//
// VfsCallbackTests.cs — VFS 回调：commit 回调（原生存裸函数指针，C# 侧保活）。
//
using System;
using System.Collections.Generic;
using NUnit.Framework;
using NativeBridgeF;

namespace NativeBridgeF.Tests.Vfs
{
    public class VfsCallbackTests
    {
        private string _dir;

        // 原生侧存裸函数指针：测试期间必须保活这些 delegate 实例（GC 回收
        // marshal thunk 会让下一次原生回调跳进已释放内存）
        private VfsCommitDelegate _commitCb;

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
        // 测试重点：commit 回调契约——每个 commit 恰好触发一次，且在提交线程
        // （本测试中即当前线程）同步触发，无需跨线程等待；回调参数 name（UTF-8
        // 解出，验证非系统 ANSI 代码页路径）/ off（4K 对齐）/ size / crc 与提交
        // 内容精确一致。delegate 保存在字段里防 GC（Editor/Mono 下无需
        // [MonoPInvokeCallback]，那是 IL2CPP/AOT 的要求）。
        public void CommitCallback_FiresWithExactArgs()
        {
            IntPtr h = VfsTestUtil.Open(_dir);
            try
            {
                var fired = new List<Tuple<string, ulong, ulong, uint>>();
                _commitCb = (user, name, off, size, crc) =>
                    fired.Add(Tuple.Create(VfsTestUtil.PtrToUtf8(name), off, size, crc));
                VfsTestUtil.AssertOk(VfsDLL.vfs_set_commit_callback(h, _commitCb, IntPtr.Zero), "set_commit_callback");

                byte[] a = VfsTestUtil.Pattern(5000, 7);
                VfsTestUtil.SeqWrite(h, "cb1.bin", a);
                VfsTestUtil.SeqWrite(h, "cb2.bin", VfsTestUtil.Pattern(10, 1));

                Assert.AreEqual(2, fired.Count);
                Assert.AreEqual("cb1.bin", fired[0].Item1);
                Assert.AreEqual(0UL, fired[0].Item2);
                Assert.AreEqual(5000UL, fired[0].Item3);
                Assert.AreEqual(VfsTestUtil.Crc32(a), fired[0].Item4);
                Assert.AreEqual("cb2.bin", fired[1].Item1);
                Assert.AreEqual(VfsTestUtil.Align4k(5000), fired[1].Item2);
            }
            finally { VfsDLL.vfs_close(h); }
        }
    }
}
