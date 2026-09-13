//
// VfsWriteTests.cs — VFS 写路径：三段式（alloc/write/commit/abort）、顺序写约束、
// 同名/空名规则、软删规则。
//
using System;
using NUnit.Framework;
using NativeBridgeF;

namespace NativeBridgeF.Tests.Vfs
{
    public class VfsWriteTests
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
        // 测试重点：三段式写 + commit 后立即可查（内存索引应答，零文件 I/O）——
        // lookup 出参逐项断言：首文件 offset 从 4K 对齐原点开始、第二文件紧随
        // 其后 4K 对齐连续摆放；size/state/crc 与提交内容精确一致（crc 由托管
        // CRC32 独立计算互验）；generation 每次 alloc+commit 恰 +2。
        public void WriteCommit_LookupFields_GenBumps()
        {
            IntPtr h = VfsTestUtil.Open(_dir);
            try
            {
                byte[] a = VfsTestUtil.Pattern(5000, 7);
                VfsTestUtil.SeqWrite(h, "a.bin", a); // alloc + commit = gen +2
                Assert.AreEqual(3UL, VfsDLL.vfs_get_generation(h));

                ulong off, size;
                int state;
                uint crc;
                VfsTestUtil.AssertOk(VfsDLL.vfs_lookup(h, VfsTestUtil.Utf8("a.bin"), out off, out size, out state, out crc), "lookup a.bin");
                Assert.AreEqual(0UL, off); // 首文件从 4K 对齐原点开始
                Assert.AreEqual(5000UL, size);
                Assert.AreEqual((int)VfsFileState.Active, state);
                Assert.AreEqual(VfsTestUtil.Crc32(a), crc);

                byte[] b = VfsTestUtil.Pattern(100, 1);
                VfsTestUtil.SeqWrite(h, "b.dat", b);
                VfsTestUtil.AssertOk(VfsDLL.vfs_lookup(h, VfsTestUtil.Utf8("b.dat"), out off, out size, out state, out crc), "lookup b.dat");
                Assert.AreEqual(VfsTestUtil.Align4k(5000), off);
                Assert.AreEqual(100UL, size);
                Assert.AreEqual(VfsTestUtil.Crc32(b), crc);
                Assert.AreEqual(5UL, VfsDLL.vfs_get_generation(h));
            }
            finally { VfsDLL.vfs_close(h); }
        }

        [Test]
        // 测试重点：commit 前置校验——未写满 [0,size) 时 commit 返回 StateInvalid
        // （不是 CrcMismatch，镜像 Rust do_commit 的水位线检查）；且 commit 失败
        // 后 writer 已被消费、由 Drop 以 abort 语义回收：条目消失（NotFound）、
        // generation 仍 +1（基线 1 + alloc + drop-abort = 3）。
        public void Commit_NotFullyWritten_StateInvalid_EntryAborted()
        {
            IntPtr h = VfsTestUtil.Open(_dir);
            try
            {
                IntPtr w = VfsTestUtil.Alloc(h, "x", 100);
                VfsTestUtil.Write(w, 0, VfsTestUtil.Pattern(40, 3), 0, 40);
                int err = VfsDLL.vfs_writer_commit(w);
                Assert.AreEqual((int)VfsResult.StateInvalid, err, "未写满 commit 应 StateInvalid");

                ulong off, size;
                int state;
                uint crc;
                Assert.AreEqual((int)VfsResult.NotFound,
                                VfsDLL.vfs_lookup(h, VfsTestUtil.Utf8("x"), out off, out size, out state, out crc));
                Assert.AreEqual(3UL, VfsDLL.vfs_get_generation(h)); // 1 基线 + alloc + drop-abort
            }
            finally { VfsDLL.vfs_close(h); }
        }

        [Test]
        // 测试重点：顺序写约束（约束：顺序写满 [0,size)）——中间留洞（rel > 游标）
        // 与越过窗口末端均返回 WriteOverflow，且失败的尝试不推进已写游标；
        // 之后按序补齐仍能 commit 成功，crc 与完整内容一致（增量 crc 未被污染）。
        public void Write_OverflowGapAndPastEnd_ThenSequentialCommit()
        {
            IntPtr h = VfsTestUtil.Open(_dir);
            try
            {
                IntPtr w = VfsTestUtil.Alloc(h, "x", 100);
                byte[] d50 = VfsTestUtil.Pattern(50, 1);
                VfsTestUtil.Write(w, 0, d50, 0, 50);

                // 中间留洞（rel 60 > 游标 50）→ WriteOverflow
                Assert.AreEqual((int)VfsResult.WriteOverflow,
                                VfsDLL.vfs_writer_write(w, 60, d50, 0, 10));
                // 越过窗口末端（50 + 51 > 100）→ WriteOverflow
                Assert.AreEqual((int)VfsResult.WriteOverflow,
                                VfsDLL.vfs_writer_write(w, 50, d50, 0, 51));

                // 失败尝试不推进游标：顺序补齐后 commit 成功且 crc 正确
                VfsTestUtil.Write(w, 50, d50, 0, 50);
                VfsTestUtil.AssertOk(VfsDLL.vfs_writer_commit(w), "commit");

                var full = new byte[100];
                System.Buffer.BlockCopy(d50, 0, full, 0, 50);
                System.Buffer.BlockCopy(d50, 0, full, 50, 50);
                ulong off, size;
                int state;
                uint crc;
                VfsTestUtil.AssertOk(VfsDLL.vfs_lookup(h, VfsTestUtil.Utf8("x"), out off, out size, out state, out crc), "lookup");
                Assert.AreEqual(VfsTestUtil.Crc32(full), crc);
            }
            finally { VfsDLL.vfs_close(h); }
        }

        [Test]
        // 测试重点：同名 alloc 规则——对 Active 条目拒绝（替换 = C# 先 delete）、
        // 对 Downloading 条目拒绝；空名拒绝（原生 InvalidArg）。注意 vfs_alloc
        // 只回成功/失败（IntPtr.Zero），不回错误码——错误码细分由 Rust 侧单测覆盖。
        public void Alloc_SameName_ActiveAndDownloading_Rejected()
        {
            IntPtr h = VfsTestUtil.Open(_dir);
            try
            {
                VfsTestUtil.SeqWrite(h, "x", VfsTestUtil.Pattern(10, 1));
                // Active 同名 → 拒绝（替换 = C# 先 delete）
                Assert.AreEqual(IntPtr.Zero, VfsDLL.vfs_alloc(h, VfsTestUtil.Utf8("x"), 10), "Active 同名应拒绝");

                // Downloading 同名 → 拒绝
                IntPtr y = VfsTestUtil.Alloc(h, "y", 10);
                Assert.AreEqual(IntPtr.Zero, VfsDLL.vfs_alloc(h, VfsTestUtil.Utf8("y"), 10), "Downloading 同名应拒绝");
                VfsTestUtil.AssertOk(VfsDLL.vfs_writer_abort(y), "abort y");

                // 空名 → 拒绝（原生 InvalidArg）
                Assert.AreEqual(IntPtr.Zero, VfsDLL.vfs_alloc(h, new byte[] { 0 }, 10), "空名应拒绝");
            }
            finally { VfsDLL.vfs_close(h); }
        }

        [Test]
        // 测试重点：Deleted 条目可被同名 alloc 替换（"替换 = 先 delete"的完整语义）——
        // 替换后旧数据区从索引剔除、转为垃圾（garbage += align4k），Deleted 计数归 0，
        // 新内容 crc 立即生效。
        public void Alloc_ReplaceDeleted_OldSpanBecomesGarbage()
        {
            IntPtr h = VfsTestUtil.Open(_dir);
            try
            {
                byte[] v1 = VfsTestUtil.Pattern(10, 1), v2 = VfsTestUtil.Pattern(10, 2);
                VfsTestUtil.SeqWrite(h, "x", v1);
                VfsTestUtil.AssertOk(VfsDLL.vfs_delete(h, VfsTestUtil.Utf8("x")), "delete");

                VfsStatInfo s = VfsTestUtil.Stat(h);
                Assert.AreEqual(VfsTestUtil.Align4k(10), s.Deleted);
                Assert.AreEqual(0UL, s.Garbage); // 旧区仍登记在册，不算垃圾

                VfsTestUtil.SeqWrite(h, "x", v2); // Deleted → 允许同名替换
                s = VfsTestUtil.Stat(h);
                Assert.AreEqual(VfsTestUtil.Align4k(10), s.Garbage); // 旧数据区成垃圾
                Assert.AreEqual(0UL, s.Deleted);                     // 旧条目已从索引剔除

                ulong off, size;
                int state;
                uint crc;
                VfsTestUtil.AssertOk(VfsDLL.vfs_lookup(h, VfsTestUtil.Utf8("x"), out off, out size, out state, out crc), "lookup");
                Assert.AreEqual(VfsTestUtil.Crc32(v2), crc);
            }
            finally { VfsDLL.vfs_close(h); }
        }

        [Test]
        // 测试重点：软删规则三分支——不存在的名字 → NotFound；Active → OK；
        // 已 Deleted 再删幂等（仍 OK）；Downloading（writer 存活期）→ StateInvalid。
        public void Delete_Rules_NotFound_Idempotent_DownloadingStateInvalid()
        {
            IntPtr h = VfsTestUtil.Open(_dir);
            try
            {
                VfsTestUtil.SeqWrite(h, "x", VfsTestUtil.Pattern(10, 1));

                // Downloading 不可删 → StateInvalid
                IntPtr y = VfsTestUtil.Alloc(h, "y", 10);
                Assert.AreEqual((int)VfsResult.StateInvalid, VfsDLL.vfs_delete(h, VfsTestUtil.Utf8("y")));
                VfsTestUtil.AssertOk(VfsDLL.vfs_writer_abort(y), "abort y");

                Assert.AreEqual((int)VfsResult.NotFound, VfsDLL.vfs_delete(h, VfsTestUtil.Utf8("nope")));
                VfsTestUtil.AssertOk(VfsDLL.vfs_delete(h, VfsTestUtil.Utf8("x")), "delete x");
                VfsTestUtil.AssertOk(VfsDLL.vfs_delete(h, VfsTestUtil.Utf8("x")), "delete x 幂等");
            }
            finally { VfsDLL.vfs_close(h); }
        }

        [Test]
        // 测试重点：abort 语义——条目从索引消失（NotFound）、已写一半的区间整体
        // 成垃圾（garbage == align4k(size)）、total 归 0；generation 恰 +2
        // （基线 1 + alloc + abort 各 +1）。
        public void Abort_RemovesEntry_SpaceBecomesGarbage()
        {
            IntPtr h = VfsTestUtil.Open(_dir);
            try
            {
                IntPtr w = VfsTestUtil.Alloc(h, "tmp", 100);
                VfsTestUtil.Write(w, 0, VfsTestUtil.Pattern(100, 9), 0, 50); // 写一半即放弃
                VfsTestUtil.AssertOk(VfsDLL.vfs_writer_abort(w), "abort");

                ulong off, size;
                int state;
                uint crc;
                Assert.AreEqual((int)VfsResult.NotFound,
                                VfsDLL.vfs_lookup(h, VfsTestUtil.Utf8("tmp"), out off, out size, out state, out crc));

                VfsStatInfo s = VfsTestUtil.Stat(h);
                Assert.AreEqual(VfsTestUtil.Align4k(100), s.Garbage);
                Assert.AreEqual(0UL, s.Total);
                Assert.AreEqual(3UL, VfsDLL.vfs_get_generation(h)); // 1 基线 + alloc + abort
            }
            finally { VfsDLL.vfs_close(h); }
        }
    }
}
