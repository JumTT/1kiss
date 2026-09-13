//
// VfsReaderTests.cs — VfsReader（mmap 只读封装）专属：映射生命周期、防撕裂闸门、
// UTF-8 文件名全链路。
//
using System;
using NUnit.Framework;
using NativeBridgeF;

namespace NativeBridgeF.Tests.Vfs
{
    public class VfsReaderTests
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
        // 测试重点：映射生命周期全由调用方掌控——未 EnsureMapping 前、
        // ReleaseMapping 后，TryReadRaw/TryReadBytes 必须安全返回 false（绝不
        // 抛异常、绝不裸解引用）；EnsureMapping 可反复建立；raw span 长度与文件
        // 大小一致；Dispose 幂等（二次调用无害）。
        public void Reader_MappingLifecycle_TryReadRawSafeFailure()
        {
            byte[] a = VfsTestUtil.Pattern(10000, 5);
            IntPtr h = VfsTestUtil.Open(_dir);
            try
            {
                VfsTestUtil.SeqWrite(h, "f1", a);
                VfsTestUtil.SeqWrite(h, "f2", VfsTestUtil.Pattern(10, 1));
            }
            finally { VfsDLL.vfs_close(h); }

            VfsReader r = VfsReader.Open(_dir);
            try
            {
                r.RefreshIndex();
                Assert.AreEqual(2, r.Count);

                VfsRawSpan span;
                Assert.IsFalse(r.TryReadRaw("f1", out span), "未建映射应安全返回 false");

                r.EnsureMapping();
                Assert.IsTrue(r.TryReadRaw("f1", out span));
                Assert.AreEqual(a.Length, span.Length);

                r.ReleaseMapping();
                byte[] ignored;
                Assert.IsFalse(r.TryReadBytes("f1", out ignored), "释放映射后应安全返回 false");

                r.EnsureMapping(); // 可重复建立
                byte[] got;
                Assert.IsTrue(r.TryReadBytes("f1", out got));
                CollectionAssert.AreEqual(a, got);
            }
            finally { r.Dispose(); }
            r.Dispose(); // 二次 Dispose 幂等
        }

        [Test]
        // 测试重点：软删 + 防撕裂闸门时序——Delete 后 RefreshIndex 前，旧索引仍
        // 放行读取（数据确实未动，不误伤）；RefreshIndex 看到 Deleted 后必须拒读
        // （唯一防撕裂闸门 = 只读 Active），其余文件不受影响；Flush 落盘后重开，
        // Deleted 状态已持久化。
        public void Reader_Delete_SoftDeleteAndAntiTearGate()
        {
            IntPtr h = VfsTestUtil.Open(_dir);
            try
            {
                VfsTestUtil.SeqWrite(h, "f1", VfsTestUtil.Pattern(100, 1));
                VfsTestUtil.SeqWrite(h, "f2", VfsTestUtil.Pattern(50, 2));
            }
            finally { VfsDLL.vfs_close(h); }

            using (var r = VfsReader.Open(_dir))
            {
                r.RefreshIndex();
                r.EnsureMapping();
                byte[] old;
                Assert.IsTrue(r.TryReadBytes("f1", out old));

                r.Delete("f1"); // 软删：数据未动，本地索引未刷新
                Assert.Greater(r.Generation, r.RefreshedGeneration);
                byte[] still;
                Assert.IsTrue(r.TryReadBytes("f1", out still), "刷新前旧索引仍放行（数据确实还在）");
                CollectionAssert.AreEqual(old, still);

                r.RefreshIndex();
                VfsEntry e;
                Assert.IsTrue(r.TryGet("f1", out e));
                Assert.AreEqual(VfsFileState.Deleted, e.State);
                byte[] refused;
                Assert.IsFalse(r.TryReadBytes("f1", out refused), "防撕裂闸门：非 Active 拒读");
                Assert.IsTrue(r.TryReadBytes("f2", out refused), "其余文件不受影响");

                r.Flush(); // delete 持久化
            }

            // 重开验证 Deleted 状态已落盘
            IntPtr h2 = VfsTestUtil.Open(_dir);
            try
            {
                ulong off, size;
                int state;
                uint crc;
                VfsTestUtil.AssertOk(VfsDLL.vfs_lookup(h2, VfsTestUtil.Utf8("f1"), out off, out size, out state, out crc), "lookup f1");
                Assert.AreEqual((int)VfsFileState.Deleted, state);
            }
            finally { VfsDLL.vfs_close(h2); }
        }

        [Test]
        // 测试重点：UTF-8 文件名全链路——alloc 入参名字经 UTF-8（NUL 结尾字节串）
        // 写入索引，RefreshIndex 从 names blob 按同样编码解出（绝不走系统 ANSI
        // 代码页/GBK），中文名与含 emoji 名都能按键读到逐字节一致的内容。
        public void Reader_Utf8Names_Roundtrip()
        {
            byte[] a = VfsTestUtil.Pattern(1000, 1), b = VfsTestUtil.Pattern(33, 2);
            IntPtr h = VfsTestUtil.Open(_dir);
            try
            {
                VfsTestUtil.SeqWrite(h, "中文文件.bin", a);
                VfsTestUtil.SeqWrite(h, "emoji📁.dat", b);
            }
            finally { VfsDLL.vfs_close(h); }

            using (var r = VfsReader.Open(_dir))
            {
                r.RefreshIndex();
                r.EnsureMapping();
                Assert.AreEqual(2, r.Count);
                byte[] ga, gb;
                Assert.IsTrue(r.TryReadBytes("中文文件.bin", out ga));
                CollectionAssert.AreEqual(a, ga);
                Assert.IsTrue(r.TryReadBytes("emoji📁.dat", out gb));
                CollectionAssert.AreEqual(b, gb);
            }
        }

        [Test]
        // 测试重点：路径式打开 VfsReader.Open(indexPath, dataPath)——自定义名容器上
        // RefreshIndex/EnsureMapping/TryReadBytes 全链路可用；IndexFilePath/DataFilePath
        // 如实回显；目录版 Open(dir) 薄壳仍走固定名对（独立空容器）；index==data 触发
        // 原生防呆返回 NULL → 封装层抛 IOException。
        public void Reader_OpenPaths_CustomContainer_FullRoundtrip()
        {
            string idx = System.IO.Path.Combine(_dir, "pack.idx");
            string dat = System.IO.Path.Combine(_dir, "pack.dat");
            byte[] a = VfsTestUtil.Pattern(3000, 9);

            IntPtr h = VfsDLL.vfs_open_paths(VfsTestUtil.Utf8(idx), VfsTestUtil.Utf8(dat));
            Assert.AreNotEqual(IntPtr.Zero, h);
            try { VfsTestUtil.SeqWrite(h, "f1", a); }
            finally { VfsDLL.vfs_close(h); }

            using (var r = VfsReader.Open(idx, dat))
            {
                Assert.AreEqual(idx, r.IndexFilePath);
                Assert.AreEqual(dat, r.DataFilePath);
                r.RefreshIndex();
                r.EnsureMapping();
                byte[] got;
                Assert.IsTrue(r.TryReadBytes("f1", out got));
                CollectionAssert.AreEqual(a, got);
            }

            // 目录版薄壳：固定名 header.vfs/files.vfs 是独立的空容器
            using (var r = VfsReader.Open(_dir))
            {
                r.RefreshIndex();
                Assert.AreEqual(0, r.Count);
                Assert.AreEqual(System.IO.Path.Combine(_dir, "header.vfs"), r.IndexFilePath);
                Assert.AreEqual(System.IO.Path.Combine(_dir, "files.vfs"), r.DataFilePath);
            }

            Assert.Throws<System.IO.IOException>(() => VfsReader.Open(idx, idx),
                "index==data 应触发原生防呆（NULL → IOException）");
        }
    }
}
