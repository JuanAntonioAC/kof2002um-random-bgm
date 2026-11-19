using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Text;
using Iced.Intel;

internal static class Program
{
    private static int Main(string[] args)
    {
        string game = args.Length > 0
            ? args[0]
            : @"D:\SteamLibrary\steamapps\common\The King of Fighters 2002 Unlimited Match";
        string outDir = args.Length > 1 ? args[1] : @"D:\KOF2002UM-RandomBGM\analysis";
        Directory.CreateDirectory(outDir);

        string exe = Path.Combine(game, "KingOfFighters2002UM_x64.exe");
        RefScan.Run(exe, Path.Combine(outDir, "bgm_refs.txt"));
        return 0;
    }

    private static List<string> DumpDurations(string game)
    {
        var lines = new List<string>();
        foreach (var dir in new[] { "UM48000Hz", "48000Hz" })
        {
            string path = Path.Combine(game, "Data", "BGM", dir);
            lines.Add("## " + dir);
            foreach (var file in Directory.GetFiles(path).OrderBy(f => f, StringComparer.OrdinalIgnoreCase))
            {
                string name = Path.GetFileName(file);
                if (!name.EndsWith(".ogg", StringComparison.OrdinalIgnoreCase))
                {
                    lines.Add($"{name}\t{new FileInfo(file).Length}\tnot-ogg");
                    continue;
                }
                double seconds = TryOggDuration(file, out int rate, out long granule);
                lines.Add(string.Format(CultureInfo.InvariantCulture,
                    "{0}\t{1:F2}s\trate={2}\tgranule={3}\tbytes={4}",
                    name, seconds, rate, granule, new FileInfo(file).Length));
            }
            lines.Add("");
        }
        foreach (var line in lines) Console.WriteLine(line);
        return lines;
    }

    private static double TryOggDuration(string path, out int sampleRate, out long granule)
    {
        sampleRate = 0;
        granule = -1;
        byte[] data = File.ReadAllBytes(path);
        int offset = 0;
        bool sawId = false;
        while (offset + 27 <= data.Length)
        {
            if (data[offset] != (byte)'O' || data[offset + 1] != (byte)'g' ||
                data[offset + 2] != (byte)'g' || data[offset + 3] != (byte)'S')
            {
                offset++;
                continue;
            }
            long pageGranule = BitConverter.ToInt64(data, offset + 6);
            int segments = data[offset + 26];
            if (offset + 27 + segments > data.Length) break;
            int body = 0;
            for (int i = 0; i < segments; i++) body += data[offset + 27 + i];
            int bodyStart = offset + 27 + segments;
            if (!sawId && bodyStart + 16 < data.Length && data[bodyStart] == 1)
            {
                // Vorbis identification header starts at the first packet.
                if (bodyStart + 7 < data.Length &&
                    data[bodyStart + 1] == (byte)'v' && data[bodyStart + 2] == (byte)'o' &&
                    data[bodyStart + 3] == (byte)'r' && data[bodyStart + 4] == (byte)'b' &&
                    data[bodyStart + 5] == (byte)'i' && data[bodyStart + 6] == (byte)'s')
                {
                    sampleRate = BitConverter.ToInt32(data, bodyStart + 12);
                    sawId = true;
                }
            }
            if (pageGranule != -1) granule = pageGranule;
            int next = bodyStart + body;
            if (next <= offset) break;
            offset = next;
        }
        if (sampleRate <= 0 || granule < 0) return -1;
        return granule / (double)sampleRate;
    }

    private static void AnalyzeExe(string exePath, string reportPath)
    {
        byte[] data = File.ReadAllBytes(exePath);
        var pe = PeImage.Parse(data);
        var sb = new StringBuilder();
        sb.AppendLine($"imageBase=0x{pe.ImageBase:X}");
        sb.AppendLine($"entry=0x{pe.ImageBase + pe.EntryRva:X}");
        foreach (var s in pe.Sections)
            sb.AppendLine($"section {s.Name} va=0x{s.VirtualAddress:X} raw=0x{s.RawOffset:X} rawSize=0x{s.RawSize:X} virtSize=0x{s.VirtualSize:X}");

        sb.AppendLine();
        sb.AppendLine("imports of interest:");
        foreach (var imp in pe.Imports)
        {
            if (imp.Dll.IndexOf("KERNEL", StringComparison.OrdinalIgnoreCase) >= 0 ||
                imp.Dll.IndexOf("DSOUND", StringComparison.OrdinalIgnoreCase) >= 0 ||
                imp.Dll.IndexOf("WINMM", StringComparison.OrdinalIgnoreCase) >= 0 ||
                imp.Dll.IndexOf("api-ms", StringComparison.OrdinalIgnoreCase) >= 0)
            {
                if (imp.Name.IndexOf("File", StringComparison.OrdinalIgnoreCase) >= 0 ||
                    imp.Name.IndexOf("Sound", StringComparison.OrdinalIgnoreCase) >= 0 ||
                    imp.Name.IndexOf("Create", StringComparison.OrdinalIgnoreCase) >= 0)
                    sb.AppendLine($"  {imp.Dll}!{imp.Name} iatRva=0x{imp.IatRva:X}");
            }
        }

        string[] needles =
        {
            "BGM/UM48000Hz/BGM15",
            "BGM/UM48000Hz/BGM41",
            "BGM/UM48000Hz/BGM42",
            "BGM/UM48000Hz/BGM01",
            "BGM/UM48000Hz/BGM12",
            "BGM/48000Hz/21",
            "BGM/48000Hz/dummy",
            "%sdata/%s.%s",
            "sound.cpp"
        };

        foreach (var needle in needles)
        {
            int off = IndexOfAscii(data, needle);
            sb.AppendLine();
            sb.AppendLine($"needle '{needle}' fileOff={(off < 0 ? "missing" : "0x" + off.ToString("X"))}");
            if (off < 0) continue;
            uint rva = pe.OffsetToRva((uint)off);
            ulong va = pe.ImageBase + rva;
            sb.AppendLine($"  rva=0x{rva:X} va=0x{va:X}");
            var hits = FindQword(data, va);
            sb.AppendLine($"  qword xrefs: {hits.Count}");
            foreach (var hit in hits.Take(12))
            {
                uint hitRva = pe.OffsetToRva((uint)hit);
                sb.AppendLine($"    file=0x{hit:X} rva=0x{hitRva:X} va=0x{pe.ImageBase + hitRva:X} sec={pe.SectionName(hitRva)}");
                if (pe.SectionName(hitRva).Trim() == ".rdata" || pe.SectionName(hitRva).Trim() == ".data")
                    DumpPointerRun(data, pe, hit, sb);
            }
            DumpRipRefs(data, pe, rva, sb);
        }

        File.WriteAllText(reportPath, sb.ToString());
        Console.WriteLine(sb.ToString());
    }

    private static void DumpPointerRun(byte[] data, PeImage pe, int hit, StringBuilder sb)
    {
        // Walk backwards and forwards while qwords look like pointers into the image.
        int start = hit;
        while (start >= 8)
        {
            ulong v = BitConverter.ToUInt64(data, start - 8);
            if (!pe.IsImageVa(v)) break;
            start -= 8;
        }
        int end = hit;
        while (end + 16 <= data.Length)
        {
            ulong v = BitConverter.ToUInt64(data, end + 8);
            if (!pe.IsImageVa(v)) break;
            end += 8;
        }
        sb.AppendLine($"    pointer run {start:X}-{end:X} count={(end - start) / 8 + 1}");
        int windowStart = Math.Max(start, hit - 16 * 8);
        int windowEnd = Math.Min(end, hit + 24 * 8);
        for (int p = windowStart; p <= windowEnd; p += 8)
        {
            ulong v = BitConverter.ToUInt64(data, p);
            string s = pe.ReadCString(data, v);
            if (s.Length > 90) s = s.Substring(0, 90);
            string mark = p == hit ? ">>" : "  ";
            sb.AppendLine($"      {mark} [{(p - start) / 8,4}] {v:X} {s}");
        }
    }

    private static void DumpRipRefs(byte[] data, PeImage pe, uint targetRva, StringBuilder sb)
    {
        var text = pe.Sections.FirstOrDefault(s => s.Name.Trim() == ".text");
        if (text == null) return;
        int found = 0;
        int start = (int)text.RawOffset;
        int end = start + (int)Math.Min(text.RawSize, text.VirtualSize);
        // Search for disp32 such that nextIp + disp == targetRva. Instruction length varies,
        // so check a window of common lea/mov encodings ending at each candidate.
        for (int i = start; i + 7 < end && found < 8; i++)
        {
            // lea reg, [rip+disp32] is 48 8D xx where xx modrm has mod=00 rm=101 (rip)
            if (data[i] != 0x48 && data[i] != 0x4C) continue;
            if (i + 6 >= end || data[i + 1] != 0x8D) continue;
            byte modrm = data[i + 2];
            if ((modrm & 0xC7) != 0x05) continue;
            int disp = BitConverter.ToInt32(data, i + 3);
            uint next = pe.OffsetToRva((uint)(i + 7));
            uint dest = (uint)(next + disp);
            if (dest != targetRva) continue;
            found++;
            uint insnRva = pe.OffsetToRva((uint)i);
            sb.AppendLine($"  rip-lea va=0x{pe.ImageBase + insnRva:X} modrm=0x{modrm:X2}");
            DisassembleAround(data, pe, i, sb);
        }
        if (found == 0) sb.AppendLine("  rip-lea: none");
    }

    private static void DisassembleAround(byte[] data, PeImage pe, int fileOff, StringBuilder sb)
    {
        int from = Math.Max((int)pe.Sections.First(s => s.Name.Trim() == ".text").RawOffset, fileOff - 32);
        // Align by decoding forward from a paragraph-ish start until we pass the site.
        var codeReader = new ByteArrayCodeReader(data, from, Math.Min(160, data.Length - from));
        var decoder = Iced.Intel.Decoder.Create(64, codeReader);
        decoder.IP = pe.ImageBase + pe.OffsetToRva((uint)from);
        ulong targetIp = pe.ImageBase + pe.OffsetToRva((uint)fileOff);
        var list = new List<Instruction>();
        ulong endIp = targetIp + 48;
        while (decoder.IP < endIp)
        {
            var instr = decoder.Decode();
            list.Add(instr);
            if (instr.Code == Code.INVALID) break;
        }
        foreach (var instr in list)
        {
            string mark = instr.IP == targetIp ? ">>" : "  ";
            sb.AppendLine($"    {mark} 0x{instr.IP:X} {instr}");
        }
    }

    private static int IndexOfAscii(byte[] data, string text)
    {
        byte[] pat = Encoding.ASCII.GetBytes(text);
        for (int i = 0; i + pat.Length < data.Length; i++)
        {
            bool ok = true;
            for (int j = 0; j < pat.Length; j++)
            {
                if (data[i + j] != pat[j]) { ok = false; break; }
            }
            if (ok) return i;
        }
        return -1;
    }

    private static List<int> FindQword(byte[] data, ulong value)
    {
        var hits = new List<int>();
        byte[] pat = BitConverter.GetBytes(value);
        for (int i = 0; i + 8 <= data.Length; i++)
        {
            bool ok = true;
            for (int j = 0; j < 8; j++)
            {
                if (data[i + j] != pat[j]) { ok = false; break; }
            }
            if (ok) hits.Add(i);
        }
        return hits;
    }

    private sealed class PeImage
    {
        public ulong ImageBase;
        public uint EntryRva;
        public List<Section> Sections = new List<Section>();
        public List<Import> Imports = new List<Import>();

        public static PeImage Parse(byte[] data)
        {
            var pe = new PeImage();
            int peOff = BitConverter.ToInt32(data, 0x3C);
            int magic = BitConverter.ToUInt16(data, peOff + 24);
            bool pe32plus = magic == 0x20B;
            pe.EntryRva = BitConverter.ToUInt32(data, peOff + 24 + 16);
            pe.ImageBase = pe32plus
                ? BitConverter.ToUInt64(data, peOff + 24 + 24)
                : BitConverter.ToUInt32(data, peOff + 24 + 28);
            ushort sections = BitConverter.ToUInt16(data, peOff + 6);
            ushort optSize = BitConverter.ToUInt16(data, peOff + 20);
            int secTable = peOff + 24 + optSize;
            for (int i = 0; i < sections; i++)
            {
                int o = secTable + i * 40;
                string name = Encoding.ASCII.GetString(data, o, 8).TrimEnd('\0');
                pe.Sections.Add(new Section
                {
                    Name = name,
                    VirtualSize = BitConverter.ToUInt32(data, o + 8),
                    VirtualAddress = BitConverter.ToUInt32(data, o + 12),
                    RawSize = BitConverter.ToUInt32(data, o + 16),
                    RawOffset = BitConverter.ToUInt32(data, o + 20)
                });
            }

            int dataDir = pe32plus ? peOff + 24 + 112 : peOff + 24 + 96;
            uint importRva = BitConverter.ToUInt32(data, dataDir + 8);
            if (importRva != 0)
            {
                int importOff = (int)pe.RvaToOffset(importRva);
                for (int n = 0; n < 64; n++)
                {
                    int o = importOff + n * 20;
                    uint nameRva = BitConverter.ToUInt32(data, o + 12);
                    uint firstThunk = BitConverter.ToUInt32(data, o + 16);
                    uint origThunk = BitConverter.ToUInt32(data, o);
                    if (nameRva == 0) break;
                    string dll = pe.ReadCString(data, pe.ImageBase + nameRva);
                    uint thunk = origThunk != 0 ? origThunk : firstThunk;
                    int thunkOff = (int)pe.RvaToOffset(thunk);
                    int iatIndex = 0;
                    for (int t = 0; t < 4096; t++)
                    {
                        ulong entry = BitConverter.ToUInt64(data, thunkOff + t * 8);
                        if (entry == 0) break;
                        string fn = "";
                        if ((entry & 0x8000000000000000UL) == 0)
                        {
                            uint hintRva = (uint)(entry & 0x7FFFFFFF);
                            int hintOff = (int)pe.RvaToOffset(hintRva);
                            fn = pe.ReadCString(data, pe.ImageBase + hintRva + 2);
                        }
                        else fn = "#" + (entry & 0xFFFF);
                        pe.Imports.Add(new Import
                        {
                            Dll = dll,
                            Name = fn,
                            IatRva = firstThunk + (uint)(iatIndex * 8)
                        });
                        iatIndex++;
                    }
                }
            }
            return pe;
        }

        public bool IsImageVa(ulong va)
        {
            if (va < ImageBase) return false;
            uint rva = (uint)(va - ImageBase);
            return Sections.Any(s => rva >= s.VirtualAddress && rva < s.VirtualAddress + Math.Max(s.VirtualSize, s.RawSize));
        }

        public uint OffsetToRva(uint offset)
        {
            foreach (var s in Sections)
            {
                if (offset >= s.RawOffset && offset < s.RawOffset + s.RawSize)
                    return s.VirtualAddress + (offset - s.RawOffset);
            }
            return offset;
        }

        public uint RvaToOffset(uint rva)
        {
            foreach (var s in Sections)
            {
                if (rva >= s.VirtualAddress && rva < s.VirtualAddress + Math.Max(s.VirtualSize, s.RawSize))
                    return s.RawOffset + (rva - s.VirtualAddress);
            }
            throw new InvalidOperationException("rva 0x" + rva.ToString("X"));
        }

        public string SectionName(uint rva)
        {
            foreach (var s in Sections)
                if (rva >= s.VirtualAddress && rva < s.VirtualAddress + Math.Max(s.VirtualSize, s.RawSize))
                    return s.Name;
            return "?";
        }

        public string ReadCString(byte[] data, ulong va)
        {
            if (!IsImageVa(va)) return "";
            uint rva = (uint)(va - ImageBase);
            int off;
            try { off = (int)RvaToOffset(rva); }
            catch { return ""; }
            var sb = new StringBuilder();
            for (int i = 0; i < 120 && off + i < data.Length; i++)
            {
                byte b = data[off + i];
                if (b == 0) break;
                if (b < 32 || b > 126) return sb.ToString();
                sb.Append((char)b);
            }
            return sb.ToString();
        }
    }

    private sealed class Section
    {
        public string Name;
        public uint VirtualSize;
        public uint VirtualAddress;
        public uint RawSize;
        public uint RawOffset;
    }

    private sealed class Import
    {
        public string Dll;
        public string Name;
        public uint IatRva;
    }
}
