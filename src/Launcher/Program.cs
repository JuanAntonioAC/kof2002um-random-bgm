using System;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;

internal static class Program
{
    private const uint CreateSuspended = 0x00000004;
    private const uint ProcessCreateThread = 0x0002;
    private const uint ProcessQueryInformation = 0x0400;
    private const uint ProcessVmOperation = 0x0008;
    private const uint ProcessVmWrite = 0x0020;
    private const uint ProcessVmRead = 0x0010;
    private const uint MemCommit = 0x1000;
    private const uint MemReserve = 0x2000;
    private const uint PageReadWrite = 0x04;

    public static int Main(string[] args)
    {
        string baseDir = AppContext.BaseDirectory;
        string launcherCfg = Path.Combine(baseDir, "launcher.cfg");
        string gameDir = @"D:\SteamLibrary\steamapps\common\The King of Fighters 2002 Unlimited Match";
        string gameExe = "KingOfFighters2002UM_x64.exe";
        string appId = "222440";
        if (File.Exists(launcherCfg))
            ReadLauncherCfg(launcherCfg, ref gameDir, ref gameExe, ref appId);

        string dll = Path.Combine(baseDir, "RandomBgm.dll");
        string tracks = Path.Combine(baseDir, "tracks.cfg");
        if (!File.Exists(dll) || !File.Exists(tracks))
        {
            Console.WriteLine("Faltan RandomBgm.dll o tracks.cfg junto al lanzador.");
            return 1;
        }

        string exePath = Path.Combine(gameDir, gameExe);
        if (!File.Exists(exePath))
        {
            Console.WriteLine("No está el ejecutable del juego: " + exePath);
            return 1;
        }

        bool injectOnly = false;
        foreach (string arg in args)
            if (arg == "--inject-only") injectOnly = true;

        Environment.SetEnvironmentVariable("SteamAppId", appId);
        Environment.SetEnvironmentVariable("SteamGameId", appId);

        Process existing = FindGame(gameExe);
        if (existing != null)
        {
            Console.WriteLine("El juego ya está abierto. Inyecto el mod en el proceso " + existing.Id + ".");
            if (!Inject(existing.Id, dll)) return 1;
            Console.WriteLine("Mod activo. Los combates nuevos usan una pista aleatoria.");
            Console.WriteLine("Registro: " + Path.Combine(baseDir, "bgm.log"));
            return 0;
        }

        if (injectOnly)
        {
            Console.WriteLine("El juego no está en ejecución.");
            return 1;
        }

        Console.WriteLine("Inicio KOF 2002 UM con el mod de BGM aleatorio.");
        if (!StartSuspended(exePath, gameDir, dll, out int pid)) return 1;
        Console.WriteLine("Proceso " + pid + " iniciado.");
        Console.WriteLine("Para jugar sin el mod, cierra esta sesión y abre el juego desde Steam.");
        Console.WriteLine("Registro: " + Path.Combine(baseDir, "bgm.log"));
        return 0;
    }

    private static void ReadLauncherCfg(string path, ref string gameDir, ref string gameExe, ref string appId)
    {
        foreach (string raw in File.ReadAllLines(path))
        {
            string line = raw.Trim();
            if (line.Length == 0 || line[0] == '#' || line[0] == ';') continue;
            int eq = line.IndexOf('=');
            if (eq <= 0) continue;
            string key = line.Substring(0, eq).Trim();
            string value = line.Substring(eq + 1).Trim();
            if (key == "game_dir") gameDir = value;
            else if (key == "game_exe") gameExe = value;
            else if (key == "app_id") appId = value;
        }
    }

    private static Process FindGame(string gameExe)
    {
        string name = Path.GetFileNameWithoutExtension(gameExe);
        Process[] list = Process.GetProcessesByName(name);
        return list.Length == 0 ? null : list[0];
    }

    private static bool StartSuspended(string exePath, string gameDir, string dll, out int pid)
    {
        pid = 0;
        var si = new StartupInfo();
        si.cb = Marshal.SizeOf(typeof(StartupInfo));
        var command = new StringBuilder("\"" + exePath + "\"");
        if (!CreateProcessW(exePath, command, IntPtr.Zero, IntPtr.Zero, false, CreateSuspended, IntPtr.Zero, gameDir, ref si, out ProcessInformation pi))
        {
            Console.WriteLine("CreateProcess falló con el error " + Marshal.GetLastWin32Error() + ".");
            return false;
        }

        pid = pi.dwProcessId;
        ResumeThread(pi.hThread);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);

        System.Threading.Thread.Sleep(800);
        Process process = null;
        try { process = Process.GetProcessById(pid); } catch { process = null; }
        if (process != null && !process.HasExited)
        {
            if (!Inject(pid, dll)) return false;
            return true;
        }

        Console.WriteLine("El primer proceso terminó. Busco el relanzamiento de Steam.");
        string name = Path.GetFileNameWithoutExtension(exePath);
        for (int i = 0; i < 40; i++)
        {
            Process[] list = Process.GetProcessesByName(name);
            foreach (Process candidate in list)
            {
                if (candidate.Id == pid) continue;
                Console.WriteLine("Inyecto en el proceso de Steam " + candidate.Id + ".");
                return Inject(candidate.Id, dll);
            }
            System.Threading.Thread.Sleep(500);
        }
        Console.WriteLine("No apareció un proceso nuevo del juego.");
        return false;
    }

    private static bool Inject(int pid, string dll)
    {
        IntPtr process = OpenProcess(ProcessCreateThread | ProcessQueryInformation | ProcessVmOperation | ProcessVmWrite | ProcessVmRead, false, pid);
        if (process == IntPtr.Zero)
        {
            Console.WriteLine("OpenProcess falló con el error " + Marshal.GetLastWin32Error() + ".");
            return false;
        }
        bool ok = Inject(process, dll);
        CloseHandle(process);
        return ok;
    }

    private static bool Inject(IntPtr process, string dll)
    {
        byte[] data = Encoding.Unicode.GetBytes(dll + "\0");
        IntPtr remote = VirtualAllocEx(process, IntPtr.Zero, (UIntPtr)data.Length, MemCommit | MemReserve, PageReadWrite);
        if (remote == IntPtr.Zero)
        {
            Console.WriteLine("VirtualAllocEx falló con el error " + Marshal.GetLastWin32Error() + ".");
            return false;
        }
        if (!WriteProcessMemory(process, remote, data, (UIntPtr)data.Length, out _))
        {
            Console.WriteLine("WriteProcessMemory falló con el error " + Marshal.GetLastWin32Error() + ".");
            return false;
        }

        IntPtr kernel = GetModuleHandleW("kernel32.dll");
        IntPtr load = GetProcAddress(kernel, "LoadLibraryW");
        if (load == IntPtr.Zero)
        {
            Console.WriteLine("No se encontró LoadLibraryW.");
            return false;
        }

        IntPtr thread = CreateRemoteThread(process, IntPtr.Zero, UIntPtr.Zero, load, remote, 0, IntPtr.Zero);
        if (thread == IntPtr.Zero)
        {
            Console.WriteLine("CreateRemoteThread falló con el error " + Marshal.GetLastWin32Error() + ".");
            return false;
        }
        WaitForSingleObject(thread, 10000);
        GetExitCodeThread(thread, out uint code);
        CloseHandle(thread);
        if (code == 0)
        {
            Console.WriteLine("LoadLibraryW no cargó la DLL en el juego.");
            return false;
        }
        return true;
    }

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    private struct StartupInfo
    {
        public int cb;
        public string lpReserved;
        public string lpDesktop;
        public string lpTitle;
        public int dwX;
        public int dwY;
        public int dwXSize;
        public int dwYSize;
        public int dwXCountChars;
        public int dwYCountChars;
        public int dwFillAttribute;
        public int dwFlags;
        public short wShowWindow;
        public short cbReserved2;
        public IntPtr lpReserved2;
        public IntPtr hStdInput;
        public IntPtr hStdOutput;
        public IntPtr hStdError;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct ProcessInformation
    {
        public IntPtr hProcess;
        public IntPtr hThread;
        public int dwProcessId;
        public int dwThreadId;
    }

    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    private static extern bool CreateProcessW(string app, StringBuilder cmd, IntPtr pa, IntPtr ta, bool inherit, uint flags, IntPtr env, string dir, ref StartupInfo si, out ProcessInformation pi);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern IntPtr OpenProcess(uint access, bool inherit, int pid);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern IntPtr VirtualAllocEx(IntPtr process, IntPtr addr, UIntPtr size, uint type, uint protect);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool WriteProcessMemory(IntPtr process, IntPtr addr, byte[] buffer, UIntPtr size, out UIntPtr written);

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode)]
    private static extern IntPtr GetModuleHandleW(string name);

    [DllImport("kernel32.dll", CharSet = CharSet.Ansi)]
    private static extern IntPtr GetProcAddress(IntPtr module, string name);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern IntPtr CreateRemoteThread(IntPtr process, IntPtr attr, UIntPtr stack, IntPtr start, IntPtr param, uint flags, IntPtr tid);

    [DllImport("kernel32.dll")]
    private static extern uint WaitForSingleObject(IntPtr handle, uint ms);

    [DllImport("kernel32.dll")]
    private static extern bool GetExitCodeThread(IntPtr thread, out uint code);

    [DllImport("kernel32.dll")]
    private static extern uint ResumeThread(IntPtr thread);

    [DllImport("kernel32.dll")]
    private static extern bool CloseHandle(IntPtr handle);
}
