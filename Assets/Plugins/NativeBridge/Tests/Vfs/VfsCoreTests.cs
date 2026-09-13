//
// VfsCoreTests.cs — VFS 基础生命周期：ABI 版本守卫、新建库初始状态、close 落盘。
//
using System;
using NUnit.Framework;
using NativeBridgeF;

namespace NativeBridgeF.Tests.Vfs
{
    public class VfsCoreTests
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
        // 测试重点：ABI 版本守卫——C# 绑定与原生库必须同为 VFS_ABI_VERSION 1，
        // 版本错位（头文件改了签名没同步 C#）第一时间在这里炸。
        public void AbiVersion_Is1()
        {
            Assert.AreEqual(1, VfsDLL.vfs_abi_version());
        }

        [Test]
        // 测试重点：新建库初始状态——generation 从 1 起（非 0，镜像 Rust roundtrip
        // 测试）、索引为空（count/blobLen == 0）、logical/total/garbage 全为 0。
        public void FreshOpen_Generation1_EmptyIndex()
        {
            IntPtr h = VfsTestUtil.Open(_dir);
            try
            {
                Assert.AreEqual(1UL, VfsDLL.vfs_get_generation(h));
                ulong gen;
                uint count, blobLen;
                VfsTestUtil.AssertOk(VfsDLL.vfs_enumerate_query(h, out gen, out count, out blobLen), "enumerate_query");
                Assert.AreEqual(1UL, gen);
                Assert.AreEqual(0u, count);
                Assert.AreEqual(0u, blobLen);

                VfsStatInfo s = VfsTestUtil.Stat(h);
                Assert.AreEqual(0UL, s.Logical);
                Assert.AreEqual(0UL, s.Total);
                Assert.AreEqual(0UL, s.Garbage);
            }
            finally { VfsDLL.vfs_close(h); }
        }

        [Test]
        // 测试重点：设计 Q11——vfs_close 自带落盘（优雅 close = flush）。不显式
        // Flush 直接 close 后重开，文件逐字节完好、generation 保持不变
        // （flush/close 不 bump generation，只有索引变化才 bump）。
        public void ClosePersists_GracefulFlushOnClose()
        {
            byte[] a = VfsTestUtil.Pattern(5000, 7), b = VfsTestUtil.Pattern(3000, 11);
            ulong gen;
            IntPtr h = VfsTestUtil.Open(_dir);
            try
            {
                VfsTestUtil.SeqWrite(h, "alpha.bin", a);
                VfsTestUtil.SeqWrite(h, "beta.dat", b);
                gen = VfsDLL.vfs_get_generation(h);
            }
            finally { VfsDLL.vfs_close(h); } // 不显式 Flush：close 自带落盘（Q11）

            using (var r = VfsReader.Open(_dir))
            {
                r.RefreshIndex();
                r.EnsureMapping(); // RefreshIndex 不建映射：读数据前必须 EnsureMapping
                Assert.AreEqual(2, r.Count);
                Assert.AreEqual(gen, r.Generation); // flush/close 不 bump generation

                byte[] gotA, gotB;
                Assert.IsTrue(r.TryReadBytes("alpha.bin", out gotA));
                CollectionAssert.AreEqual(a, gotA);
                Assert.IsTrue(r.TryReadBytes("beta.dat", out gotB));
                CollectionAssert.AreEqual(b, gotB);
            }
        }
    }
}
