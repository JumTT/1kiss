//
// VfsBulkTests.cs — 批量读写与删除重开，按 native VFS 契约编写：
// 批量写 → close 持久化 → 重开逐文件字节复验、Stat 无碎片记账；
// 批量删 → Deleted 记账精确、软删条目跨重开保留并拒读（防撕裂），
// 由整理 Finalize 回收（见 VfsCompactionTests 批量用例）。
//
using System;
using System.Collections.Generic;
using NUnit.Framework;
using NativeBridgeF;

namespace NativeBridgeF.Tests.Vfs
{
    public class VfsBulkTests
    {
        private string _dir;

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

        // 直接枚举 4K 对齐临界尺寸（EditMode 秒级、失败可复现）
        private static readonly int[] Sizes = { 1, 4095, 4096, 4097, 1 << 16 };

        private static List<byte[]> WriteBulk(IntPtr h, int rounds)
        {
            var content = new List<byte[]>();
            for (int r = 0; r < rounds; r++)
            {
                for (int i = 0; i < Sizes.Length; i++)
                {
                    byte[] d = VfsTestUtil.Pattern(Sizes[i], (byte)(r * Sizes.Length + i + 1));
                    content.Add(d);
                    VfsTestUtil.SeqWrite(h, "bulk" + content.Count, d);
                }
            }
            return content;
        }

        [Test]
        // 批量写 → close（close 即 flush 持久化）→ 重开 → 数量一致、逐文件内容
        // 精确一致；stat 无碎片：total == 各文件 4K 对齐和、physical == logical（追加写）。
        public void BulkWrite_CloseReopen_ContentIntact()
        {
            List<byte[]> content;
            IntPtr h = VfsTestUtil.Open(_dir);
            try { content = WriteBulk(h, 4); }
            finally { VfsDLL.vfs_close(h); }

            using (var r = VfsReader.Open(_dir))
            {
                r.RefreshIndex();
                Assert.AreEqual(content.Count, r.Count);
                r.EnsureMapping();
                for (int i = 0; i < content.Count; i++)
                {
                    byte[] got;
                    Assert.IsTrue(r.TryReadBytes("bulk" + (i + 1), out got), "bulk" + (i + 1));
                    CollectionAssert.AreEqual(content[i], got);
                }

                VfsStatInfo s = r.Stat();
                ulong expectTotal = 0;
                foreach (byte[] d in content) expectTotal += VfsTestUtil.Align4k((ulong)d.Length);
                Assert.AreEqual(expectTotal, s.Total);
                Assert.AreEqual(0UL, s.Deleted);
                Assert.AreEqual(0UL, s.Garbage);
                Assert.AreEqual(s.Logical, s.Physical);
            }
        }

        [Test]
        // 批量写 → 删一半 → Deleted 记账精确、Garbage == 0（被删区仍登记在册）→ close 重开 →
        // 幸存者内容完好；软删条目跨重开保留（State=Deleted，TryReadBytes 拒读 =
        // 防撕裂闸门），等整理 Finalize 回收——见 VfsCompactionTests 的批量用例。
        public void BulkDelete_Reopen_SurvivorsIntact()
        {
            List<byte[]> content;
            IntPtr h = VfsTestUtil.Open(_dir);
            try
            {
                content = WriteBulk(h, 4);
                ulong expectDeleted = 0;
                for (int i = 0; i < content.Count; i += 2)
                {
                    VfsTestUtil.AssertOk(VfsDLL.vfs_delete(h, VfsTestUtil.Utf8("bulk" + (i + 1))), "delete bulk" + (i + 1));
                    expectDeleted += VfsTestUtil.Align4k((ulong)content[i].Length);
                }

                VfsStatInfo s = VfsTestUtil.Stat(h);
                Assert.AreEqual(expectDeleted, s.Deleted);
                Assert.AreEqual(0UL, s.Garbage);
            }
            finally { VfsDLL.vfs_close(h); }

            using (var r = VfsReader.Open(_dir))
            {
                r.RefreshIndex();
                Assert.AreEqual(content.Count, r.Count); // 软删条目重开保留（open 只丢 Downloading）
                r.EnsureMapping();
                for (int i = 0; i < content.Count; i++)
                {
                    string name = "bulk" + (i + 1);
                    VfsEntry e;
                    Assert.IsTrue(r.TryGet(name, out e), name);
                    byte[] got;
                    if (i % 2 == 0)
                    {
                        Assert.AreEqual(VfsFileState.Deleted, e.State, name);
                        Assert.IsFalse(r.TryReadBytes(name, out got), name + " 已删应拒读");
                    }
                    else
                    {
                        Assert.AreEqual(VfsFileState.Active, e.State, name);
                        Assert.IsTrue(r.TryReadBytes(name, out got), name);
                        CollectionAssert.AreEqual(content[i], got, name);
                    }
                }
            }
        }
    }
}
