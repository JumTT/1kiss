//
// VfsRecoveryTests.cs — 损坏恢复白盒矩阵：按磁盘格式精确篡改 header.vfs/files.vfs
// 字节后重开，断言 native 恢复契约（回退、重置、补零、残留丢弃）。
//
// 白盒依据（VFS 磁盘格式，format_version=1，全部小端）：
//   header.vfs = SuperBlock(64B) + region 槽顺序排布（等 gen 重复 persist 合法，
//   故 close 的 best-effort flush 会让最新快照存在两份等价副本——回退断言按槽位写）。
//   SuperBlock: magic[8]="1KVFSHDR" | format_version u32@8 | active_gen u64@12
//               | sb_crc u32@20（覆盖前 20 字节）。
//   Region 头(32B): gen u64@0 | entry_count u32@8 | string_area_len u32@12
//               | region_crc u32@16（覆盖 EntryTable+StringArea）| logical u64@20。
//   Entry(32B): entry_crc@28 覆盖前 28 字节。
//   open 恢复（mod.rs）：SB 失效 → 取校验通过槽中 gen 最高者；无合法槽 → 重置空库；
//   任一 entry 坏 = 整区作废（all-or-nothing，恢复粒度是整快照而非单文件）；
//   count/str_len 超防呆上限或槽位被截断 → 顺序扫描终止；Downloading 残留丢弃成垃圾。
// 断言失败 = 预编译二进制与上述格式契约不符 → 本用例即复现器。
//
using System;
using System.Collections.Generic;
using System.IO;
using NUnit.Framework;
using NativeBridgeF;

namespace NativeBridgeF.Tests.Vfs
{
    public class VfsRecoveryTests
    {
        private const int SBSize = 64;        // SuperBlock 定长 64B
        private const int RegionHdrSize = 32; // Region 头定长 32B

        private string _dir;
        private string HeaderPath { get { return Path.Combine(_dir, "header.vfs"); } }

        [SetUp]
        public void SetUp() { _dir = VfsTestUtil.NewTempDir(); }

        [TearDown]
        public void TearDown()
        {
            try
            {
                if (Directory.Exists(_dir)) Directory.Delete(_dir, true);
            }
            catch { } // 偶发句柄占用留给系统临时目录清理，不令测试失败
        }

        private sealed class Slot
        {
            public int Off;      // header.vfs 内槽位起点
            public ulong Gen;
            public uint Count;   // entry_count
        }

        /// <summary>顺序解析健康 header.vfs 的槽位布局（与 scan_slot 同规则：EOF 即停）。</summary>
        private static List<Slot> ParseSlots(byte[] hdr)
        {
            var slots = new List<Slot>();
            int off = SBSize;
            while (off + RegionHdrSize <= hdr.Length)
            {
                ulong gen = BitConverter.ToUInt64(hdr, off);
                uint count = BitConverter.ToUInt32(hdr, off + 8);
                uint strLen = BitConverter.ToUInt32(hdr, off + 12);
                long total = RegionHdrSize + (long)count * 32 + strLen;
                if (count > (1u << 24) || strLen > (1u << 30) || off + total > hdr.Length) break;
                slots.Add(new Slot { Off = off, Gen = gen, Count = count });
                off += (int)total;
            }
            return slots;
        }

        /// <summary>建库：两次 flush 产出两个数据代（gen3: f1；gen5: f1+f2），close 再补一份 gen5 副本。</summary>
        private static void BuildTwoGenLibrary(string dir)
        {
            IntPtr h = VfsTestUtil.Open(dir);
            try
            {
                VfsTestUtil.SeqWrite(h, "f1", VfsTestUtil.Pattern(5000, 1));
                VfsTestUtil.AssertOk(VfsDLL.vfs_flush(h), "flush#1");
                VfsTestUtil.SeqWrite(h, "f2", VfsTestUtil.Pattern(3000, 2));
                VfsTestUtil.AssertOk(VfsDLL.vfs_flush(h), "flush#2");
            }
            finally { VfsDLL.vfs_close(h); }
        }

        [Test]
        // active 槽 body 任一字节坏 → region_crc/entry_crc 失配 → 整区不采信
        // （all-or-nothing：单条 entry 坏也是整区作废）；close 落了两份同 gen
        // 快照 → 回退前一份健康副本：f1+f2 完好、gen 不变。
        public void ActiveRegionCorrupt_FallsBackToHealthySnapshot()
        {
            BuildTwoGenLibrary(_dir);
            byte[] hdr = File.ReadAllBytes(HeaderPath);
            List<Slot> slots = ParseSlots(hdr);
            Slot active = slots[slots.Count - 1];
            hdr[active.Off + RegionHdrSize] ^= 0xFF; // EntryTable 首条 entry 的首字节
            File.WriteAllBytes(HeaderPath, hdr);

            using (var r = VfsReader.Open(_dir))
            {
                r.RefreshIndex();
                Assert.AreEqual(2, r.Count);
                Assert.AreEqual(5UL, r.Generation);
                r.EnsureMapping();
                byte[] got;
                Assert.IsTrue(r.TryReadBytes("f1", out got), "f1");
                Assert.IsTrue(r.TryReadBytes("f2", out got), "f2");
            }
        }

        [Test]
        // 回退旧代路径：gen 最高的快照全部作废（两份 gen5 各坏一字节）→ 回退
        // gen3：只剩 f1（内容逐字节完好）、f2 消失、generation 倒回。
        // 崩溃丢失上限 = 最后一次已 persist 的增量。
        public void AllMaxGenRegionsCorrupt_RollsBackToOlderGen()
        {
            BuildTwoGenLibrary(_dir);
            byte[] hdr = File.ReadAllBytes(HeaderPath);
            List<Slot> slots = ParseSlots(hdr);
            ulong maxGen = slots[slots.Count - 1].Gen;
            foreach (Slot s in slots)
            {
                if (s.Gen == maxGen && s.Count > 0) hdr[s.Off + RegionHdrSize] ^= 0xFF;
            }
            File.WriteAllBytes(HeaderPath, hdr);

            using (var r = VfsReader.Open(_dir))
            {
                r.RefreshIndex();
                Assert.AreEqual(1, r.Count);
                Assert.AreEqual(3UL, r.Generation);
                r.EnsureMapping();
                byte[] got;
                Assert.IsTrue(r.TryReadBytes("f1", out got), "f1");
                CollectionAssert.AreEqual(VfsTestUtil.Pattern(5000, 1), got);
                VfsEntry gone;
                Assert.IsFalse(r.TryGet("f2", out gone));
            }
        }

        [Test]
        // 表文件尾部截断在 active 槽位中段 → scan_slot 因 EOF 终止 → 回退上一份
        // 健康快照（恢复粒度是整快照，不做部分条目加载）。
        public void TruncatedTrailingRegion_FallsBackToHealthySnapshot()
        {
            BuildTwoGenLibrary(_dir);
            List<Slot> slots = ParseSlots(File.ReadAllBytes(HeaderPath));
            int cut = slots[slots.Count - 1].Off + RegionHdrSize + 8; // active 槽 body 中段

            using (var fs = new FileStream(HeaderPath, FileMode.Open, FileAccess.ReadWrite))
            {
                fs.SetLength(cut);
            }

            using (var r = VfsReader.Open(_dir))
            {
                r.RefreshIndex();
                Assert.AreEqual(2, r.Count);
                Assert.AreEqual(5UL, r.Generation);
            }
        }

        [Test]
        // 表文件全坏：header.vfs 整个成垃圾 → open 成功但重置空库（gen=1），
        // 文件重建为 SB+空 region（96 字节）；数据区残留被忽略（logical=0）。
        public void HeaderGarbage_ResetsToEmptyLibrary()
        {
            BuildTwoGenLibrary(_dir);
            File.WriteAllBytes(HeaderPath, VfsTestUtil.Pattern(500, 9));

            using (var r = VfsReader.Open(_dir))
            {
                r.RefreshIndex();
                Assert.AreEqual(0, r.Count);
                Assert.AreEqual(1UL, r.Generation);
                Assert.AreEqual(SBSize + RegionHdrSize, new FileInfo(HeaderPath).Length); // 重建
            }
        }

        [Test]
        // SB 的 sb_crc 单独坏 → SB 失效但槽位完好 → open 取 gen 最高合法槽，
        // active 内容不丢（这是"SB 只翻一半"时的容错路径）。
        public void SbCrcCorrupt_HighestGenValidStillPicked()
        {
            BuildTwoGenLibrary(_dir);
            byte[] hdr = File.ReadAllBytes(HeaderPath);
            hdr[20] ^= 0xFF; // sb_crc @20..24
            File.WriteAllBytes(HeaderPath, hdr);

            using (var r = VfsReader.Open(_dir))
            {
                r.RefreshIndex();
                Assert.AreEqual(2, r.Count);
                Assert.AreEqual(5UL, r.Generation);
            }
        }

        [Test]
        // data 文件删除：重开自动重建并补零到 logical；索引条目原样（Active、
        // size/crc 元数据不变），读出全零内容、读者算出的 Crc32 与条目 crc 不符
        // ——判损留给读者侧（CRC_MISMATCH 错误码为读者预留，原生不主动产生）。
        public void DataFileDeleted_ReopenZeroFilled_CrcMismatchDetectable()
        {
            byte[] f1c = VfsTestUtil.Pattern(5000, 1);
            IntPtr h = VfsTestUtil.Open(_dir);
            try
            {
                VfsTestUtil.SeqWrite(h, "f1", f1c);
                VfsTestUtil.AssertOk(VfsDLL.vfs_flush(h), "flush");
            }
            finally { VfsDLL.vfs_close(h); }

            File.Delete(Path.Combine(_dir, "files.vfs"));

            using (var r = VfsReader.Open(_dir))
            {
                r.RefreshIndex();
                Assert.AreEqual(1, r.Count);
                VfsEntry e;
                Assert.IsTrue(r.TryGet("f1", out e));
                Assert.AreEqual(VfsFileState.Active, e.State);
                Assert.AreEqual(5000UL, e.Size);
                Assert.AreEqual(VfsTestUtil.Crc32(f1c), e.Crc); // 元数据完好

                r.EnsureMapping();
                byte[] got;
                Assert.IsTrue(r.TryReadBytes("f1", out got));
                CollectionAssert.AreEqual(new byte[5000], got); // 内容全零
                Assert.AreNotEqual(VfsTestUtil.Crc32(f1c), VfsTestUtil.Crc32(got)); // 读者可判损
            }
        }

        [Test]
        // 写中断崩溃：writer 存活期 flush 把 Downloading 条目落盘，"崩溃"（泄漏
        // writer，与原生 mem::forget 同款）后重开 → 条目丢弃、区间成垃圾；
        // 已 commit 的 keep 完好。
        // 注意：泄漏的 writer 持有 files.vfs 句柄，本用例临时目录会残留到进程退出。
        public void DownloadingLeftover_DroppedAsGarbage()
        {
            IntPtr h = VfsTestUtil.Open(_dir);
            try
            {
                VfsTestUtil.SeqWrite(h, "keep", VfsTestUtil.Pattern(100, 1));
                IntPtr w = VfsTestUtil.Alloc(h, "dl", 5000); // 崩溃点：不 commit 也不 abort
                VfsTestUtil.AssertOk(VfsDLL.vfs_flush(h), "flush");

                VfsStatInfo s = VfsTestUtil.Stat(h);
                Assert.AreEqual(0UL, s.Garbage); // writer 存活期区间仍算预约
            }
            finally { VfsDLL.vfs_close(h); } // best-effort flush，泄漏的 writer 无碍 close

            using (var r = VfsReader.Open(_dir))
            {
                r.RefreshIndex();
                VfsEntry e;
                Assert.IsFalse(r.TryGet("dl", out e));
                Assert.IsTrue(r.TryGet("keep", out e));
                Assert.AreEqual(VfsFileState.Active, e.State);
                Assert.AreEqual(VfsTestUtil.Align4k(5000), r.Stat().Garbage);
            }
        }
    }
}
