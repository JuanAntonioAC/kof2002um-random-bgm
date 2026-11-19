using System;
using System.IO;
using System.Text;
using System.Collections.Generic;
using System.Linq;

class Program {
  static void Main(string[] args) {
    string path = args[0];
    string outAscii = args[1];
    byte[] data = File.ReadAllBytes(path);
    Console.WriteLine($"size={data.Length}");
    if (data.Length > 2 && data[0]==0x4D && data[1]==0x5A) Console.WriteLine("MZ header ok");
    var ascii = new List<string>();
    var sb = new StringBuilder();
    Action flush = () => { if (sb.Length >= 5) ascii.Add(sb.ToString()); sb.Clear(); };
    foreach (byte b in data) {
      if (b >= 32 && b < 127) sb.Append((char)b); else flush();
    }
    flush();
    File.WriteAllLines(outAscii, ascii);
    Console.WriteLine($"ascii_strings={ascii.Count}");

    var utf16 = new List<string>();
    var sb2 = new StringBuilder();
    for (int i = 0; i + 1 < data.Length; i += 2) {
      byte lo = data[i], hi = data[i+1];
      if (hi == 0 && lo >= 32 && lo < 127) sb2.Append((char)lo);
      else {
        if (sb2.Length >= 5) utf16.Add(sb2.ToString());
        sb2.Clear();
        if (hi != 0) {
          // realign if odd
        }
      }
    }
    if (sb2.Length >= 5) utf16.Add(sb2.ToString());
    // also scan odd alignment
    sb2.Clear();
    for (int i = 1; i + 1 < data.Length; i += 2) {
      byte lo = data[i], hi = data[i+1];
      if (hi == 0 && lo >= 32 && lo < 127) sb2.Append((char)lo);
      else { if (sb2.Length >= 5) utf16.Add(sb2.ToString()); sb2.Clear(); }
    }
    if (sb2.Length >= 5) utf16.Add(sb2.ToString());
    string outUtf = args[2];
    File.WriteAllLines(outUtf, utf16.Distinct());
    Console.WriteLine($"utf16_strings={utf16.Distinct().Count()}");

    string[] keys = new[] { "BGM", "ogg", "Ogg", "vorbis", "Vorbis", "FMOD", "fmod", "XAudio", "48000", "CreateFile", "Sound", ".wav", "loop", "LOOP", "bass", "OpenAL", "wasapi", "DirectSound", "Data\\", "Data/" };
    Console.WriteLine("--- ASCII MATCHES ---");
    foreach (var s in ascii.Where(s => keys.Any(k => s.IndexOf(k, StringComparison.OrdinalIgnoreCase) >= 0)).Distinct())
      Console.WriteLine(s);
    Console.WriteLine("--- UTF16 MATCHES ---");
    foreach (var s in utf16.Distinct().Where(s => keys.Any(k => s.IndexOf(k, StringComparison.OrdinalIgnoreCase) >= 0)))
      Console.WriteLine(s);
  }
}
