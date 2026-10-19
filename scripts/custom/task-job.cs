/**
 * @file scripts/custom/task-job.cs
 * @brief Own an entire Sunshine process tree across native self-restarts.
 */
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;

/** @brief A non-inheritable kill-on-close Windows job for the fixed host. */
public sealed class SunshineTaskJob : IDisposable {
    private IntPtr job; ///< The supervisor alone owns this job handle.

    /** @brief Native job process and working-set limits. */
    [StructLayout(LayoutKind.Sequential)]
    private struct BasicLimits {
        public long ProcessTime, JobTime; ///< Unused CPU time limits.
        public uint Flags; ///< Kill-on-close and explicit application breakaway flags.
        public UIntPtr MinimumWorkingSet, MaximumWorkingSet; ///< Unused working-set limits.
        public uint ProcessLimit; ///< Unused process-count limit.
        public UIntPtr Affinity; ///< Unused affinity mask.
        public uint Priority, Scheduling; ///< Unused scheduling limits.
    }
    /** @brief Native job I/O counters. */
    [StructLayout(LayoutKind.Sequential)]
    private struct IoCounters {
        public ulong ReadOperations, WriteOperations, OtherOperations; ///< Operation counts.
        public ulong ReadBytes, WriteBytes, OtherBytes; ///< Transfer counts.
    }
    /** @brief Native extended job limits. */
    [StructLayout(LayoutKind.Sequential)]
    private struct ExtendedLimits {
        public BasicLimits Basic; ///< Process limits.
        public IoCounters Io; ///< I/O counters.
        public UIntPtr ProcessMemory, JobMemory, PeakProcessMemory, PeakJobMemory; ///< Unused memory limits.
    }
    /** @brief Native process accounting, including descendants. */
    [StructLayout(LayoutKind.Sequential)]
    private struct Accounting {
        public long UserTime, KernelTime, PeriodUserTime, PeriodKernelTime; ///< CPU accounting.
        public uint Faults, TotalProcesses, ActiveProcesses, TerminatedProcesses; ///< Process accounting.
    }
    /** @brief Native startup state for a hidden, private console. */
    [StructLayout(LayoutKind.Sequential, CharSet=CharSet.Unicode)]
    private struct StartupInfo {
        public uint Size; ///< Structure size.
        public string Reserved, Desktop, Title; ///< Optional strings.
        public uint X, Y, Width, Height, Columns, Rows, Attributes, Flags; ///< Window settings.
        public ushort ShowWindow, ReservedBytes; ///< SW_HIDE and reserved length.
        public IntPtr ReservedPointer, Input, Output, Error; ///< Optional handles.
    }
    /** @brief Native process creation result. */
    [StructLayout(LayoutKind.Sequential)]
    private struct ProcessInfo {
        public IntPtr Process, Thread; ///< Caller-owned handles.
        public uint ProcessId, ThreadId; ///< Created identifiers.
    }
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    private static extern IntPtr CreateJobObject(IntPtr attributes, string name); ///< Create a private job.
    [DllImport("kernel32.dll", SetLastError=true)]
    private static extern bool SetInformationJobObject(IntPtr handle, int type, ref ExtendedLimits info, int size); ///< Set kill-on-close limits.
    [DllImport("kernel32.dll", SetLastError=true)]
    private static extern bool QueryInformationJobObject(IntPtr handle, int type, out Accounting info, int size, IntPtr length); ///< Count live descendants.
    [DllImport("kernel32.dll", SetLastError=true)]
    private static extern bool AssignProcessToJobObject(IntPtr handle, IntPtr process); ///< Attach before any host code executes.
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    private static extern bool CreateProcess(string app, StringBuilder args, IntPtr pa, IntPtr ta, bool inherit, uint flags, IntPtr env, string cwd, ref StartupInfo startup, out ProcessInfo process); ///< Create the host suspended.
    [DllImport("kernel32.dll", SetLastError=true)]
    private static extern uint ResumeThread(IntPtr thread); ///< Release the attached host.
    [DllImport("kernel32.dll", SetLastError=true)]
    private static extern bool TerminateProcess(IntPtr process, uint code); ///< Clean up an unattached suspended process on failure.
    [DllImport("kernel32.dll")]
    private static extern bool CloseHandle(IntPtr handle); ///< Release a kernel handle.

    /** @brief Create a job whose complete process tree ends when its supervisor exits. */
    public SunshineTaskJob() {
        job=CreateJobObject(IntPtr.Zero,null);
        if(job==IntPtr.Zero) throw new Win32Exception(Marshal.GetLastWin32Error());
        // Streamed applications explicitly use CREATE_BREAKAWAY_FROM_JOB; native host restarts do not.
        var limits=new ExtendedLimits(); limits.Basic.Flags=0x2800;
        if(!SetInformationJobObject(job,9,ref limits,Marshal.SizeOf(typeof(ExtendedLimits)))) {
            int error=Marshal.GetLastWin32Error(); Dispose(); throw new Win32Exception(error);
        }
    }
    /**
     * @brief Launch the fixed host with no race before job membership is established.
     * @param executable Absolute executable path.
     * @param commandLine Complete, quoted Windows command line including argv zero.
     * @param directory Host working directory.
     * @return Initial host process identifier.
     */
    public uint Start(string executable, string commandLine, string directory) {
        if(job==IntPtr.Zero) throw new ObjectDisposedException("SunshineTaskJob");
        var startup=new StartupInfo(); startup.Size=(uint)Marshal.SizeOf(typeof(StartupInfo)); startup.Flags=1;
        ProcessInfo process;
        // CREATE_SUSPENDED | CREATE_NEW_CONSOLE | CREATE_UNICODE_ENVIRONMENT.
        // A hidden console preserves Sunshine's normal self-restart behavior.
        if(!CreateProcess(executable,new StringBuilder(commandLine),IntPtr.Zero,IntPtr.Zero,false,0x414,IntPtr.Zero,directory,ref startup,out process)) throw new Win32Exception(Marshal.GetLastWin32Error());
        try {
            if(!AssignProcessToJobObject(job,process.Process)) throw new Win32Exception(Marshal.GetLastWin32Error());
            if(ResumeThread(process.Thread)==uint.MaxValue) throw new Win32Exception(Marshal.GetLastWin32Error());
        } catch { TerminateProcess(process.Process,1); throw; }
        finally { CloseHandle(process.Thread); CloseHandle(process.Process); }
        return process.ProcessId;
    }
    /** @brief Get the number of active processes, including restarted hosts. */
    public uint ActiveProcesses {
        get {
            if(job==IntPtr.Zero) throw new ObjectDisposedException("SunshineTaskJob");
            Accounting info;
            if(!QueryInformationJobObject(job,1,out info,Marshal.SizeOf(typeof(Accounting)),IntPtr.Zero)) throw new Win32Exception(Marshal.GetLastWin32Error());
            return info.ActiveProcesses;
        }
    }
    /** @brief Remain attached until the host and every self-restarted descendant exits. */
    public void Wait() { while(ActiveProcesses>0) Thread.Sleep(250); }
    /** @brief Terminate all remaining descendants and release the job, once. */
    public void Dispose() { if(job!=IntPtr.Zero) { CloseHandle(job); job=IntPtr.Zero; } }
}
