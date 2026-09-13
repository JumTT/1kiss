//
// VfsConcurrentTests.cs — 同句柄写读并发：后台线程写新文件 + 主线程并发读已有文件。
//
// 契约（vfs.h / mod.rs）：句柄内部全锁线程安全，writer 限创建线程使用；commit
// 不搬已提交数据、追加只写新预留区间 → 读旧区间与追加并发安全。同库双开是
// UNSUPPORTED（会互翻 SuperBlock），故主线程直读 files.vfs 而非再开句柄。
// 用例失败 = 原生 bug：本仓库只有预编译二进制，测试保留为复现器。
//
using System;
using System.Collections.Generic;
using System.IO;
using System.Threading;
using NUnit.Framework;
using NativeBridgeF;

namespace NativeBridgeF.Tests.Vfs
{
    public class VfsConcurrentTests
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

        [Test]
        // 基线 8 文件（64KB）→ 后台线程顺序 commit 8 新文件（256KB，writer 仅在
        // 创建线程使用）→ 主线程反复 lookup + 直读 files.vfs 校验基线内容
        // （do-while 保证至少一轮；是否真正交错取决于调度——不变量对零次/多次
        // 交错都成立）→ join → generation 恰 1+2×16、同句柄 enumerate 全量清点 →
        // 重开逐文件字节复验。
        public void ReadBaselineWhileBackgroundWrites()
        {
            const int baseCount = 8, newCount = 8;
            const int baseSize = 1 << 16, newSize = 1 << 18;

            var baseContent = new List<byte[]>();
            var newContent = new List<byte[]>(); // 仅后台线程写入，Join 后主线程读取
            var baseOff = new ulong[baseCount];

            IntPtr h = VfsTestUtil.Open(_dir);
            try
            {
                for (int i = 0; i < baseCount; i++)
                {
                    byte[] d = VfsTestUtil.Pattern(baseSize, (byte)(i + 1));
                    baseContent.Add(d);
                    VfsTestUtil.SeqWrite(h, "base" + i, d);
                }
                var baseCrc = new uint[baseCount];
                for (int i = 0; i < baseCount; i++)
                {
                    ulong off, size; int state; uint crc;
                    VfsTestUtil.AssertOk(VfsDLL.vfs_lookup(h, VfsTestUtil.Utf8("base" + i), out off, out size, out state, out crc), "base" + i);
                    Assert.AreEqual(baseSize, (int)size);
                    Assert.AreEqual((int)VfsFileState.Active, state);
                    baseCrc[i] = crc;
                    baseOff[i] = off;
                }

                Exception bgError = null;
                var bgDone = new ManualResetEvent(false);
                var writer = new Thread(() =>
                {
                    try
                    {
                        for (int j = 0; j < newCount; j++)
                        {
                            byte[] d = VfsTestUtil.Pattern(newSize, (byte)(100 + j));
                            VfsTestUtil.SeqWrite(h, "new" + j, d);
                            newContent.Add(d);
                        }
                    }
                    catch (Exception ex) { bgError = ex; }
                    finally { bgDone.Set(); }
                });

                writer.Start();
                int sweeps = 0;
                using (var fs = new FileStream(System.IO.Path.Combine(_dir, "files.vfs"),
                                               FileMode.Open, FileAccess.Read, FileShare.ReadWrite))
                {
                    var buf = new byte[baseSize];
                    do // 至少一轮：对每条基线 lookup + 读原始字节 + CRC 互验
                    {
                        for (int i = 0; i < baseCount; i++)
                        {
                            ulong off, size; int state; uint crc;
                            VfsTestUtil.AssertOk(VfsDLL.vfs_lookup(h, VfsTestUtil.Utf8("base" + i), out off, out size, out state, out crc), "base" + i);
                            Assert.AreEqual(baseCrc[i], crc, "base" + i + " 元数据被并发污染");

                            fs.Seek((long)baseOff[i], SeekOrigin.Begin);
                            int got = 0;
                            while (got < baseSize)
                            {
                                int n = fs.Read(buf, 0, baseSize - got);
                                Assert.Greater(n, 0, "base" + i + " 读到 EOF");
                                got += n;
                            }
                            Assert.AreEqual(baseCrc[i], VfsTestUtil.Crc32(buf), "base" + i + " 内容被并发污染");
                        }
                        sweeps++;
                    } while (!bgDone.WaitOne(0));
                }
                writer.Join();
                if (bgError != null) throw bgError;
                Assert.GreaterOrEqual(sweeps, 1);

                Assert.AreEqual(1UL + 2UL * (baseCount + newCount), VfsDLL.vfs_get_generation(h));

                ulong gen; uint count, blobLen;
                VfsTestUtil.AssertOk(VfsDLL.vfs_enumerate_query(h, out gen, out count, out blobLen), "enumerate_query");
                Assert.AreEqual(baseCount + newCount, (int)count);
                var names = new byte[blobLen];
                var offs = new ulong[count]; var sizes = new ulong[count];
                var crcs = new uint[count]; var states = new int[count];
                VfsTestUtil.AssertOk(VfsDLL.vfs_enumerate_read(h, gen, names, offs, sizes, crcs, states, (UIntPtr)count), "enumerate_read");
                foreach (int st in states) Assert.AreEqual((int)VfsFileState.Active, st);
            }
            finally { VfsDLL.vfs_close(h); }

            using (var r = VfsReader.Open(_dir))
            {
                r.RefreshIndex();
                Assert.AreEqual(baseCount + newCount, r.Count);
                r.EnsureMapping();
                for (int i = 0; i < baseCount; i++)
                {
                    byte[] got;
                    Assert.IsTrue(r.TryReadBytes("base" + i, out got), "base" + i);
                    CollectionAssert.AreEqual(baseContent[i], got);
                }
                for (int j = 0; j < newCount; j++)
                {
                    byte[] got;
                    Assert.IsTrue(r.TryReadBytes("new" + j, out got), "new" + j);
                    CollectionAssert.AreEqual(newContent[j], got);
                }
            }
        }
    }
}
