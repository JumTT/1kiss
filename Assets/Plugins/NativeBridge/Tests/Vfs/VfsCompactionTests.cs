//
// VfsCompactionTests.cs — VFS 异步碎片整理：公开 API 全流程、确定性 Busy 断言。
//
// 不抓整理中间态（Scan/Move/Finalize 各阶段）：小数据集整理微秒级完成，抓不到；
// 要确定性抓住需 ~256MB 数据集，不值。竞争语义由 Rust 侧 compact_busy_rules 覆盖。
//
using System;
using System.Collections.Generic;
using System.Threading;
using NUnit.Framework;
using NativeBridgeF;

namespace NativeBridgeF.Tests.Vfs
{
    public class VfsCompactionTests
    {
        private string _dir;

        // 原生侧存裸函数指针：测试期间必须保活这些 delegate 实例
        private VfsProgressDelegate _progressCb;
        private VfsDoneDelegate _doneCb;

        [SetUp]
        public void SetUp() { _dir = VfsTestUtil.NewTempDir(); }

        [TearDown]
        public void TearDown()
        {
            try
            {
                if (System.IO.Directory.Exists(_dir)) System.IO.Directory.Delete(_dir, true);
            }
            catch { } // 偶发句柄占用留给系统临时目录清理，不令测试失败
        }

        [Test]
        // 测试重点：整理全流程（C# 公开 API 端到端，即 T6 延后联调的 C# 侧部分）——
        // 写 6 个文件（~4.5MB）→ 删 2 个 → ReleaseMapping（Windows：映射存续期间
        // 无法 truncate，整理前必须释放）→ 异步整理 done 回调 err==OK → RefreshIndex
        // 后 Deleted 条目消失、搬迁后 Active 内容逐字节完好（防撕裂根基：commit 后
        // 数据不因整理损坏）→ stat 满足 physical == logical + reserve（截垃圾 +
        // 预扩容一步到位）、garbage/deleted 归 0 → Finalize bump generation →
        // 有搬迁就有进度回调。注意次序：整理后先 RefreshIndex 再 EnsureMapping。
        public void Reader_Compact_FullCycle()
        {
            int[] sizes = { 1 << 20, 700001, 1 << 20, 500001, 1 << 20, 300001 };
            var content = new List<byte[]>();
            IntPtr h = VfsTestUtil.Open(_dir);
            try
            {
                for (int i = 0; i < sizes.Length; i++)
                {
                    content.Add(VfsTestUtil.Pattern(sizes[i], (byte)(i + 1)));
                    VfsTestUtil.SeqWrite(h, "f" + (i + 1), content[i]);
                }
            }
            finally { VfsDLL.vfs_close(h); }

            using (var r = VfsReader.Open(_dir))
            {
                r.RefreshIndex();
                r.EnsureMapping();
                Assert.AreEqual(6, r.Count);
                byte[] pre;
                Assert.IsTrue(r.TryReadBytes("f3", out pre));
                CollectionAssert.AreEqual(content[2], pre);

                r.Delete("f2");
                r.Delete("f5");
                r.RefreshIndex();
                VfsStatInfo s = r.Stat();
                Assert.AreEqual(VfsTestUtil.Align4k(700001) + VfsTestUtil.Align4k(1 << 20), s.Deleted);

                r.ReleaseMapping(); // Windows：映射存续期间无法 truncate，整理前必须释放

                int progressCount = 0;
                int doneErr = -1;
                var doneEvent = new ManualResetEvent(false);
                _progressCb = (user, percent, name) => Interlocked.Increment(ref progressCount);
                _doneCb = (user, err) =>
                {
                    doneErr = err;
                    doneEvent.Set();
                };
                ulong genBefore = r.Generation;

                r.Compact(1UL << 20, _progressCb, _doneCb, IntPtr.Zero);
                Assert.IsTrue(doneEvent.WaitOne(15000), "compaction done 超时");
                Assert.AreEqual((int)VfsResult.OK, doneErr, "整理失败");

                r.RefreshIndex(); // 整理后先刷新索引，再重建映射（offset 可能整体偏移）
                Assert.AreEqual(4, r.Count);
                VfsEntry gone;
                Assert.IsFalse(r.TryGet("f2", out gone));
                Assert.IsFalse(r.TryGet("f5", out gone));

                r.EnsureMapping();
                for (int i = 0; i < sizes.Length; i++)
                {
                    if (i == 1 || i == 4) continue;
                    byte[] got;
                    Assert.IsTrue(r.TryReadBytes("f" + (i + 1), out got), "f" + (i + 1));
                    CollectionAssert.AreEqual(content[i], got); // 搬迁后内容完好
                }

                s = r.Stat();
                Assert.AreEqual(0UL, s.Deleted);
                Assert.AreEqual(0UL, s.Garbage);
                Assert.AreEqual(s.Logical + (1UL << 20), s.Physical); // 截垃圾 + 预扩容一步到位
                Assert.Greater(r.Generation, genBefore);              // Finalize bump
                Assert.GreaterOrEqual(progressCount, 1);              // 有搬迁即有进度回调
            }
        }

        [Test]
        // 测试重点：整理入口的确定性拒绝与收尾——writer 存活期 vfs_compact 必返回
        // BusyWriters（无 writer 时才有资格整理），compact_status 平时可查（Idle）；
        // writer 释放后整理可正常发起，done 回调传 null 时靠轮询 status 回到 Idle
        // 收尾；reserve=0 时整理后物理大小恰等于新逻辑大小（set_len 截垃圾）。
        public void Compact_BusyWriters_ThenRunsToIdle()
        {
            IntPtr h = VfsTestUtil.Open(_dir);
            try
            {
                VfsTestUtil.SeqWrite(h, "f1", VfsTestUtil.Pattern(5000, 1));

                int state, percent;
                VfsTestUtil.AssertOk(VfsDLL.vfs_compact_status(h, out state, out percent), "status");
                Assert.AreEqual((int)VfsCompactState.Idle, state);

                IntPtr w = VfsTestUtil.Alloc(h, "f2", 100); // writer 存活 → 整理必须被拒（确定性）
                Assert.AreEqual((int)VfsResult.BusyWriters,
                                VfsDLL.vfs_compact(h, 0, null, null, IntPtr.Zero));
                VfsTestUtil.AssertOk(VfsDLL.vfs_writer_abort(w), "abort");

                // writer 释放后整理可跑：done 回调传 null，轮询状态到 Idle 收尾
                VfsTestUtil.AssertOk(VfsDLL.vfs_compact(h, 0, null, null, IntPtr.Zero), "compact");
                var deadline = DateTime.UtcNow.AddSeconds(15);
                do
                {
                    VfsTestUtil.AssertOk(VfsDLL.vfs_compact_status(h, out state, out percent), "status");
                    if (state == (int)VfsCompactState.Idle) break;
                    Thread.Sleep(10);
                } while (DateTime.UtcNow < deadline);
                Assert.AreEqual((int)VfsCompactState.Idle, state, "整理未在超时内完成");

                VfsStatInfo s = VfsTestUtil.Stat(h);
                Assert.AreEqual(s.Logical, s.Physical); // reserve=0：物理恰为新逻辑大小
            }
            finally { VfsDLL.vfs_close(h); }
        }
    }
}
