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

        [Test]
        // 测试重点：路径式打开 vfs_open_paths——索引/数据文件分别指定（自定义文件名、
        // 异目录），父目录按需创建；close 自带落盘（Q11），重开 lookup 的内容状态/crc
        // 完好；目录版薄壳（vfs_open = 固定名 header.vfs + files.vfs）是独立的另一对，
        // 与自定义容器互不串扰（镜像 Rust open_paths_custom_names）。
        public void OpenPaths_CustomNames_DifferentDirs_IndependentOfDirOpen()
        {
            string idx = System.IO.Path.Combine(_dir, "sub1", "base.idx");
            string dat = System.IO.Path.Combine(_dir, "sub2", "base.dat");
            byte[] a = VfsTestUtil.Pattern(5000, 3);

            IntPtr h = VfsDLL.vfs_open_paths(VfsTestUtil.Utf8(idx), VfsTestUtil.Utf8(dat));
            Assert.AreNotEqual(IntPtr.Zero, h, "vfs_open_paths " + idx);
            try { VfsTestUtil.SeqWrite(h, "f1", a); }
            finally { VfsDLL.vfs_close(h); }

            Assert.IsTrue(System.IO.File.Exists(idx), "索引文件应按需创建于自定义路径");
            Assert.IsTrue(System.IO.File.Exists(dat), "数据文件应按需创建于自定义路径");

            IntPtr h2 = VfsDLL.vfs_open_paths(VfsTestUtil.Utf8(idx), VfsTestUtil.Utf8(dat));
            Assert.AreNotEqual(IntPtr.Zero, h2, "重开自定义容器");
            try
            {
                ulong off, size; int state; uint crc;
                VfsTestUtil.AssertOk(VfsDLL.vfs_lookup(h2, VfsTestUtil.Utf8("f1"),
                    out off, out size, out state, out crc), "lookup");
                Assert.AreEqual((ulong)a.Length, size);
                Assert.AreEqual((int)VfsFileState.Active, state);
                Assert.AreEqual(VfsTestUtil.Crc32(a), crc);
            }
            finally { VfsDLL.vfs_close(h2); }

            // 目录版薄壳：固定名是另一对文件——空库（generation=1）、无自定义容器内容
            IntPtr h3 = VfsTestUtil.Open(_dir);
            try
            {
                Assert.AreEqual(1UL, VfsDLL.vfs_get_generation(h3));
                ulong off, size; int state; uint crc;
                Assert.AreEqual((int)VfsResult.NotFound,
                    VfsDLL.vfs_lookup(h3, VfsTestUtil.Utf8("f1"), out off, out size, out state, out crc),
                    "目录版固定名容器不应看到自定义容器的内容");
            }
            finally { VfsDLL.vfs_close(h3); }
        }

        [Test]
        // 测试重点：vfs_open_paths 防呆——同一路径（含大小写差异，Windows 文件名
        // 不区分大小写）、空路径、纯空白路径一律返回 NULL（INVALID_ARG），绝不打开
        // 成"索引=数据"的毁库形态；且防呆不误伤正常的不同路径打开
        // （镜像 Rust open_paths_rejects_bad_args）。
        public void OpenPaths_RejectsBadArgs()
        {
            string p = System.IO.Path.Combine(_dir, "same.vfs");
            string upper = System.IO.Path.Combine(_dir, "SAME.VFS");
            Assert.AreEqual(IntPtr.Zero, VfsDLL.vfs_open_paths(VfsTestUtil.Utf8(p), VfsTestUtil.Utf8(p)),
                "同一路径应被拒绝");
            Assert.AreEqual(IntPtr.Zero, VfsDLL.vfs_open_paths(VfsTestUtil.Utf8(p), VfsTestUtil.Utf8(upper)),
                "大小写差异的同一路径应被拒绝");
            Assert.AreEqual(IntPtr.Zero, VfsDLL.vfs_open_paths(VfsTestUtil.Utf8(""), VfsTestUtil.Utf8(p)),
                "空索引路径应被拒绝");
            Assert.AreEqual(IntPtr.Zero, VfsDLL.vfs_open_paths(VfsTestUtil.Utf8(p), VfsTestUtil.Utf8("   ")),
                "纯空白数据路径应被拒绝");

            IntPtr ok = VfsDLL.vfs_open_paths(VfsTestUtil.Utf8(p),
                VfsTestUtil.Utf8(System.IO.Path.Combine(_dir, "other.vfs")));
            Assert.AreNotEqual(IntPtr.Zero, ok, "不同路径不应被防呆误伤");
            VfsDLL.vfs_close(ok);
        }
    }
}
