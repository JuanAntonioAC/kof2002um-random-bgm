using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using Iced.Intel;

internal static class RefScan
{
    public static void Run(string exePath, string reportPath)
    {
        byte[] data = File.ReadAllBytes(exePath);
        var pe = Pe.Parse(data);
        var sb = new StringBuilder();

        int pool = IndexOf(data, "BGM/UM48000Hz/BGM00");
        sb.AppendLine($"poolFile=0x{pool:X} rva=0x{pe.OffsetToRva((uint)pool):X}");
        int poolEnd = IndexOf(data, "BGM/48000Hz/dummy");
        poolEnd = IndexOf(data, "\0", poolEnd) + 1;
        sb.AppendLine($"poolEndFile=0x{poolEnd:X}");

        sb.AppendLine("--- preamble ---");
        int pre = Math.Max(0, pool - 128);
        sb.AppendLine(HexAscii(data, pre, pool - pre));

        uint poolRva = pe.OffsetToRva((uint)pool);
        uint poolEndRva = pe.OffsetToRva((uint)poolEnd);
        sb.AppendLine($"poolRva=0x{poolRva:X}-0x{poolEndRva:X}");

        // 32-bit RVA hits for the first few track paths.
        foreach (var label in new[] { "BGM/UM48000Hz/BGM00", "BGM/UM48000Hz/BGM12", "BGM/UM48000Hz/BGM15", "BGM/UM48000Hz/BGM41", "BGM00" })
        {
            int off = IndexOf(data, label + "\0");
            uint rva = pe.OffsetToRva((uint)off);
            int hits = CountDword(data, rva);
            sb.AppendLine($"dword rva 0x{rva:X} '{label}' hits={hits}");
        }

        var text = pe.Sections.First(s => s.Name.Trim() == ".text");
        int textStart = (int)text.RawOffset;
        int textEnd = textStart + (int)text.RawSize;
        var leaHits = new List<string>();
        for (int i = textStart; i + 7 < textEnd; i++)
        {
            if (data[i] != 0x48 && data[i] != 0x4C) continue;
            if (data[i + 1] != 0x8D) continue;
            byte modrm = data[i + 2];
            if ((modrm & 0xC7) != 0x05) continue;
            int disp = BitConverter.ToInt32(data, i + 3);
            uint next = pe.OffsetToRva((uint)(i + 7));
            uint dest = (uint)(next + disp);
            if (dest >= poolRva && dest < poolEndRva)
            {
                uint insn = pe.OffsetToRva((uint)i);
                string s = pe.ReadCString(data, pe.ImageBase + dest);
                leaHits.Add($"lea va=0x{pe.ImageBase + insn:X} dest=0x{dest:X} '{s}'");
            }
        }
        sb.AppendLine($"lea hits into pool: {leaHits.Count}");
        foreach (var h in leaHits.Take(80)) sb.AppendLine(h);

        // mov reg, imm64 into pool VA range
        ulong poolVa = pe.ImageBase + poolRva;
        ulong poolEndVa = pe.ImageBase + poolEndRva;
        int immHits = 0;
        for (int i = textStart; i + 10 < textEnd; i++)
        {
            if ((data[i] != 0x48 && data[i] != 0x49) || data[i + 1] < 0xB8 || data[i + 1] > 0xBF) continue;
            ulong imm = BitConverter.ToUInt64(data, i + 2);
            if (imm < poolVa || imm >= poolEndVa) continue;
            immHits++;
            if (immHits <= 40)
            {
                uint insn = pe.OffsetToRva((uint)i);
                sb.AppendLine($"movimm va=0x{pe.ImageBase + insn:X} imm=0x{imm:X} '{pe.ReadCString(data, imm)}'");
            }
        }
        sb.AppendLine($"movimm hits: {immHits}");

        // Function containing the path formatter.
        uint fmtRva = pe.OffsetToRva((uint)IndexOf(data, "%sdata/%s.%s"));
        // known lea site from previous run
        uint known = 0x662D7;
        var fn = pe.FunctionOf(known);
        sb.AppendLine($"formatter string rva=0x{fmtRva:X}");
        sb.AppendLine(fn.Begin == 0
            ? "formatter function: not in pdata"
            : $"formatter function 0x{pe.ImageBase + fn.Begin:X}-0x{pe.ImageBase + fn.End:X}");

        if (fn.Begin != 0)
        {
            var calls = FindCalls(data, pe, text, fn.Begin);
            sb.AppendLine($"calls to formatter: {calls.Count}");
            foreach (var c in calls.Take(30))
            {
                var caller = pe.FunctionOf(c);
                sb.AppendLine($"  call va=0x{pe.ImageBase + c:X} in 0x{pe.ImageBase + caller.Begin:X}-0x{pe.ImageBase + caller.End:X}");
            }
            sb.AppendLine();
            sb.AppendLine("==== formatter ====");
            sb.AppendLine(Disasm(data, pe, fn.Begin, fn.End));
            foreach (var target in new uint[] { 0x65E30, 0x66120 })
            {
                var f = pe.FunctionOf(target);
                sb.AppendLine();
                sb.AppendLine($"==== function 0x{pe.ImageBase + f.Begin:X} ====");
                sb.AppendLine(Disasm(data, pe, f.Begin, f.End));
                var callers = FindCalls(data, pe, text, f.Begin);
                sb.AppendLine($"callers: {callers.Count}");
                foreach (var c in callers.Take(20))
                {
                    var caller = pe.FunctionOf(c);
                    sb.AppendLine($"  call va=0x{pe.ImageBase + c:X} in 0x{pe.ImageBase + caller.Begin:X}-0x{pe.ImageBase + caller.End:X} size=0x{caller.End - caller.Begin:X}");
                }
            }
        }

        File.WriteAllText(reportPath, sb.ToString());
        Console.WriteLine(sb.ToString());
    }

    private static List<uint> FindCalls(byte[] data, Pe pe, Section text, uint targetRva)
    {
        var hits = new List<uint>();
        int start = (int)text.RawOffset;
        int end = start + (int)text.RawSize;
        for (int i = start; i + 5 < end; i++)
        {
            if (data[i] != 0xE8) continue;
            int rel = BitConverter.ToInt32(data, i + 1);
            uint next = pe.OffsetToRva((uint)(i + 5));
            uint dest = (uint)(next + rel);
            if (dest == targetRva)
                hits.Add(pe.OffsetToRva((uint)i));
        }
        return hits;
    }

    private static string Disasm(byte[] data, Pe pe, uint beginRva, uint endRva)
    {
        int start = (int)pe.RvaToOffset(beginRva);
        int len = (int)(endRva - beginRva);
        if (len > 0x800) len = 0x800;
        var reader = new ByteArrayCodeReader(data, start, len);
        var decoder = Iced.Intel.Decoder.Create(64, reader);
        decoder.IP = pe.ImageBase + beginRva;
        var sb = new StringBuilder();
        while (decoder.IP < pe.ImageBase + beginRva + (uint)len)
        {
            var instr = decoder.Decode();
            if (instr.Code == Code.INVALID) break;
            sb.AppendLine($"0x{instr.IP:X} {instr}");
        }
        return sb.ToString();
    }

    private static string HexAscii(byte[] data, int off, int len)
    {
        var sb = new StringBuilder();
        for (int i = 0; i < len; i += 16)
        {
            sb.Append($"{off + i:X8}  ");
            int n = Math.Min(16, len - i);
            for (int j = 0; j < n; j++) sb.Append(data[off + i + j].ToString("X2") + " ");
            sb.Append(" ");
            for (int j = 0; j < n; j++)
            {
                byte b = data[off + i + j];
                sb.Append(b >= 32 && b < 127 ? (char)b : '.');
            }
            sb.AppendLine();
        }
        return sb.ToString();
    }

    private static int CountDword(byte[] data, uint value)
    {
        byte[] pat = BitConverter.GetBytes(value);
        int count = 0;
        for (int i = 0; i + 4 <= data.Length; i++)
        {
            if (data[i] == pat[0] && data[i + 1] == pat[1] && data[i + 2] == pat[2] && data[i + 3] == pat[3])
                count++;
        }
        return count;
    }

    private static int IndexOf(byte[] data, string text)
    {
        return IndexOf(data, text, 0);
    }

    private static int IndexOf(byte[] data, string text, int start)
    {
        byte[] pat = Encoding.ASCII.GetBytes(text);
        for (int i = start; i + pat.Length <= data.Length; i++)
        {
            bool ok = true;
            for (int j = 0; j < pat.Length; j++)
                if (data[i + j] != pat[j]) { ok = false; break; }
            if (ok) return i;
        }
        return -1;
    }

    private sealed class Section
    {
        public string Name;
        public uint VirtualAddress;
        public uint VirtualSize;
        public uint RawSize;
        public uint RawOffset;
    }

    private struct Func
    {
        public uint Begin;
        public uint End;
    }

    private sealed class Pe
    {
        public ulong ImageBase;
        public List<Section> Sections = new List<Section>();
        private uint[] begins;
        private uint[] ends;

        public static Pe Parse(byte[] data)
        {
            var pe = new Pe();
            int peOff = BitConverter.ToInt32(data, 0x3C);
            pe.ImageBase = BitConverter.ToUInt64(data, peOff + 24 + 24);
            ushort sections = BitConverter.ToUInt16(data, peOff + 6);
            ushort optSize = BitConverter.ToUInt16(data, peOff + 20);
            int secTable = peOff + 24 + optSize;
            for (int i = 0; i < sections; i++)
            {
                int o = secTable + i * 40;
                pe.Sections.Add(new Section
                {
                    Name = Encoding.ASCII.GetString(data, o, 8).TrimEnd('\0'),
                    VirtualSize = BitConverter.ToUInt32(data, o + 8),
                    VirtualAddress = BitConverter.ToUInt32(data, o + 12),
                    RawSize = BitConverter.ToUInt32(data, o + 16),
                    RawOffset = BitConverter.ToUInt32(data, o + 20)
                });
            }
            var pdata = pe.Sections.First(s => s.Name.Trim() == ".pdata");
            int count = (int)(pdata.RawSize / 12);
            pe.begins = new uint[count];
            pe.ends = new uint[count];
            int raw = (int)pdata.RawOffset;
            for (int i = 0; i < count; i++)
            {
                pe.begins[i] = BitConverter.ToUInt32(data, raw + i * 12);
                pe.ends[i] = BitConverter.ToUInt32(data, raw + i * 12 + 4);
            }
            return pe;
        }

        public Func FunctionOf(uint rva)
        {
            int lo = 0, hi = begins.Length - 1, ans = -1;
            while (lo <= hi)
            {
                int mid = (lo + hi) >> 1;
                if (begins[mid] <= rva) { ans = mid; lo = mid + 1; }
                else hi = mid - 1;
            }
            if (ans < 0) return default;
            if (rva >= begins[ans] && rva < ends[ans]) return new Func { Begin = begins[ans], End = ends[ans] };
            return default;
        }

        public uint RvaToOffset(uint rva)
        {
            foreach (var s in Sections)
                if (rva >= s.VirtualAddress && rva < s.VirtualAddress + Math.Max(s.VirtualSize, s.RawSize))
                    return s.RawOffset + (rva - s.VirtualAddress);
            throw new InvalidOperationException("rva");
        }

        public uint OffsetToRva(uint offset)
        {
            foreach (var s in Sections)
                if (offset >= s.RawOffset && offset < s.RawOffset + s.RawSize)
                    return s.VirtualAddress + (offset - s.RawOffset);
            return offset;
        }

        public string ReadCString(byte[] data, ulong va)
        {
            uint rva = (uint)(va - ImageBase);
            uint off = 0;
            bool found = false;
            foreach (var s in Sections)
            {
                if (rva >= s.VirtualAddress && rva < s.VirtualAddress + Math.Max(s.VirtualSize, s.RawSize))
                {
                    off = s.RawOffset + (rva - s.VirtualAddress);
                    found = true;
                    break;
                }
            }
            if (!found || off >= data.Length) return "";
            var sb = new StringBuilder();
            for (int i = 0; i < 80 && off + i < data.Length; i++)
            {
                byte b = data[off + i];
                if (b == 0) break;
                if (b < 32 || b > 126) break;
                sb.Append((char)b);
            }
            return sb.ToString();
        }
    }
}
