//
// VfsQueryTests.cs — VFS 查询：enumerate 两段式协议、stat 空间会计。
//
using System;
using NUnit.Framework;
using NativeBridgeF;

namespace NativeBridgeF.Tests.Vfs
{
    public class VfsQueryTests
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
        // 测试重点：enumerate 两段式协议——query 返回 gen/count/blobLen；read 的
        // cap < count 时返回 BufferTooSmall（防止调用方缓冲不足被静默截断）；
        // 足额读取后：names blob（NUL 结尾背靠背）解析出的名字集合、crc 集合与
        // 内容互验、state 全为 Active。
        public void EnumerateRead_CapTooSmall_ThenOk()
        {
            IntPtr h = VfsTestUtil.Open(_dir);
            try
            {
                byte[] d1 = VfsTestUtil.Pattern(10, 1), d2 = VfsTestUtil.Pattern(10, 2);
                VfsTestUtil.SeqWrite(h, "f1", d1);
                VfsTestUtil.SeqWrite(h, "f2", d2);

                ulong gen;
                uint count, blobLen;
                VfsTestUtil.AssertOk(VfsDLL.vfs_enumerate_query(h, out gen, out count, out blobLen), "query");
                Assert.AreEqual(2u, count);
                Assert.AreEqual(5UL, gen); // 1 基线 + 2 × (alloc+commit)

                var names = new byte[blobLen];
                var offs = new ulong[1];
                var sizes = new ulong[1];
                var crcs = new uint[1];
                var states = new int[1];
                Assert.AreEqual((int)VfsResult.BufferTooSmall,
                                VfsDLL.vfs_enumerate_read(h, gen, names, offs, sizes, crcs, states, (UIntPtr)1),
                                "cap < count 应 BufferTooSmall");

                offs = new ulong[2];
                sizes = new ulong[2];
                crcs = new uint[2];
                states = new int[2];
                VfsTestUtil.AssertOk(VfsDLL.vfs_enumerate_read(h, gen, names, offs, sizes, crcs, states, (UIntPtr)2), "read");

                CollectionAssert.AreEquivalent(new[] { "f1", "f2" }, VfsTestUtil.ParseNames(names));
                CollectionAssert.AreEquivalent(new uint[] { VfsTestUtil.Crc32(d1), VfsTestUtil.Crc32(d2) }, crcs);
                Assert.AreEqual((int)VfsFileState.Active, states[0]);
                Assert.AreEqual((int)VfsFileState.Active, states[1]);
            }
            finally { VfsDLL.vfs_close(h); }
        }

        [Test]
        // 测试重点：stat 空间会计在各阶段的精确推演——5000B 写入按 4K 对齐记
        // 8192；软删后 Active→Deleted 但不算垃圾（仍登记在册）；Downloading
        // 条目计入 total（writer 存活期是"预约"）；abort 后区间才转 garbage。
        // 全程 physical ≥ logical（预扩容富余）。
        public void Stat_AccountingAcrossStages()
        {
            IntPtr h = VfsTestUtil.Open(_dir);
            try
            {
                VfsStatInfo s = VfsTestUtil.Stat(h);
                Assert.AreEqual(0UL, s.Logical);
                Assert.AreEqual(0UL, s.Total);
                Assert.AreEqual(0UL, s.Active);
                Assert.AreEqual(0UL, s.Deleted);
                Assert.AreEqual(0UL, s.Garbage);

                VfsTestUtil.SeqWrite(h, "f1", VfsTestUtil.Pattern(5000, 3)); // 5000 → 4K 对齐 8192
                s = VfsTestUtil.Stat(h);
                Assert.AreEqual(8192UL, s.Logical);
                Assert.AreEqual(8192UL, s.Total);
                Assert.AreEqual(8192UL, s.Active);
                Assert.AreEqual(0UL, s.Deleted);
                Assert.AreEqual(0UL, s.Garbage);
                Assert.GreaterOrEqual(s.Physical, s.Logical); // 预扩容富余

                VfsTestUtil.AssertOk(VfsDLL.vfs_delete(h, VfsTestUtil.Utf8("f1")), "delete");
                s = VfsTestUtil.Stat(h);
                Assert.AreEqual(0UL, s.Active);
                Assert.AreEqual(8192UL, s.Deleted);
                Assert.AreEqual(0UL, s.Garbage); // Deleted 仍登记在册，不算垃圾

                IntPtr w = VfsTestUtil.Alloc(h, "f2", 100); // Downloading：计入 total，不算 Active/Deleted
                s = VfsTestUtil.Stat(h);
                Assert.AreEqual(12288UL, s.Logical);
                Assert.AreEqual(12288UL, s.Total);
                Assert.AreEqual(8192UL, s.Deleted);
                Assert.AreEqual(0UL, s.Garbage); // writer 存活期区间仍是预约

                VfsTestUtil.AssertOk(VfsDLL.vfs_writer_abort(w), "abort");
                s = VfsTestUtil.Stat(h);
                Assert.AreEqual(12288UL, s.Logical);
                Assert.AreEqual(8192UL, s.Total);
                Assert.AreEqual(4096UL, s.Garbage); // abort 区间成垃圾
            }
            finally { VfsDLL.vfs_close(h); }
        }
    }
}
