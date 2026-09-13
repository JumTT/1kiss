//
// VfsTestUtil.cs — VFS 测试共享工具。
//
// 提供各功能目录下的测试共用的：UTF-8 字节编解码（NUL 结尾，镜像 DlmgrDLL.Utf8Bytes
// 的 ABI 约定，但只用公开 API 不依赖 internal）、zlib 兼容 CRC32（与原生
// format::crc32 同源，用于对 lookup/enumerate 回报的 crc 做独立互验）、
// alloc→全量写→commit 的 seq_write 便捷封装、内容模式串生成、stat 便捷读取、
// enumerate 名字 blob 解析等。
//
// 断言语义镜像 rust/src/modules/vfs/mod.rs 的 #[cfg(test)] 行为。
//
using System;
using System.Collections.Generic;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using NUnit.Framework;
using NativeBridgeF;

namespace NativeBridgeF.Tests.Vfs
{
    internal static class VfsTestUtil
    {
        /// <summary>每个用例独立临时目录（SetUp 创建 / TearDown 删除），互不污染。</summary>
        public static string NewTempDir()
        {
            return Path.Combine(Path.GetTempPath(), "nbvfs_tests", Guid.NewGuid().ToString("N"));
        }

        /// <summary>UTF-8 + NUL 结尾字节串（跨 ABI 字符串的统一编解码）。</summary>
        public static byte[] Utf8(string s)
        {
            byte[] b = new byte[Encoding.UTF8.GetByteCount(s) + 1];
            Encoding.UTF8.GetBytes(s, 0, s.Length, b, 0); // 尾部字节保持 0
            return b;
        }

        public static IntPtr Open(string dir)
        {
            IntPtr h = VfsDLL.vfs_open(Utf8(dir));
            Assert.AreNotEqual(IntPtr.Zero, h, "vfs_open " + dir);
            return h;
        }

        public static void AssertOk(int err, string api)
        {
            Assert.AreEqual((int)VfsResult.OK, err, api + " -> " + (VfsResult)err);
        }

        public static IntPtr Alloc(IntPtr h, string name, ulong size)
        {
            IntPtr w = VfsDLL.vfs_alloc(h, Utf8(name), size);
            Assert.AreNotEqual(IntPtr.Zero, w, "vfs_alloc " + name);
            return w;
        }

        public static void Write(IntPtr w, ulong relOff, byte[] data, int offset, int count)
        {
            AssertOk(VfsDLL.vfs_writer_write(w, relOff, data, offset, count), "vfs_writer_write");
        }

        /// <summary>alloc → 全量顺序写 → commit（镜像 Rust 测试的 seq_write）。</summary>
        public static void SeqWrite(IntPtr h, string name, byte[] data)
        {
            IntPtr w = Alloc(h, name, (ulong)data.Length);
            Write(w, 0, data, 0, data.Length);
            AssertOk(VfsDLL.vfs_writer_commit(w), "vfs_writer_commit " + name);
        }

        /// <summary>确定性的伪随机内容（同 len+seed 必得同内容，crc 断言可复现）。</summary>
        public static byte[] Pattern(int len, byte seed)
        {
            var b = new byte[len];
            for (int i = 0; i < len; i++) b[i] = (byte)(seed + i * 7 % 251);
            return b;
        }

        public static ulong Align4k(ulong n)
        {
            return (n + 4095UL) & ~4095UL;
        }

        // zlib 兼容 CRC32（IEEE 反射式，多项式 0xEDB88320）
        private static readonly uint[] CrcTable = BuildCrcTable();

        private static uint[] BuildCrcTable()
        {
            var t = new uint[256];
            for (uint i = 0; i < 256; i++)
            {
                uint c = i;
                for (int k = 0; k < 8; k++) c = (c & 1) != 0 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
                t[i] = c;
            }
            return t;
        }

        public static uint Crc32(byte[] d)
        {
            uint c = 0xFFFFFFFFu;
            foreach (byte b in d) c = CrcTable[(c ^ b) & 0xFF] ^ (c >> 8);
            return c ^ 0xFFFFFFFFu;
        }

        /// <summary>读回调/枚举里拿到的 NUL 结尾 UTF-8 C 字符串。</summary>
        public static string PtrToUtf8(IntPtr p)
        {
            int len = 0;
            while (Marshal.ReadByte(p, len) != 0) len++;
            var b = new byte[len];
            Marshal.Copy(p, b, 0, len);
            return Encoding.UTF8.GetString(b);
        }

        public static VfsStatInfo Stat(IntPtr h)
        {
            ulong logical, physical, total, active, deleted, garbage;
            AssertOk(VfsDLL.vfs_stat(h, out logical, out physical, out total,
                                     out active, out deleted, out garbage), "vfs_stat");
            return new VfsStatInfo
            {
                Logical = logical, Physical = physical, Total = total,
                Active = active, Deleted = deleted, Garbage = garbage,
            };
        }

        /// <summary>解析 enumerate_read 的 names blob（NUL 结尾名字背靠背存放）。</summary>
        public static List<string> ParseNames(byte[] blob)
        {
            var list = new List<string>();
            int start = 0;
            while (start < blob.Length)
            {
                int end = start;
                while (end < blob.Length && blob[end] != 0) end++;
                list.Add(Encoding.UTF8.GetString(blob, start, end - start));
                start = end + 1;
            }
            return list;
        }
    }
}
