# Windows Shell file operations support MTP objects without converting their
# paths into disk paths. Callers must verify the exact parent before deletion.
if(-not ('NinMagicMtpOps' -as [type])) {
Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
[ComImport, Guid("43826D1E-E718-42EE-BC55-A1E261C37BFE"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface INinMagicShellItem {
 void BindToHandler(IntPtr pbc, ref Guid bhid, ref Guid riid, out IntPtr ppv);
 void GetParent(out INinMagicShellItem parent);
 void GetDisplayName(uint sigdn, out IntPtr name);
 void GetAttributes(uint mask, out uint attributes);
 void Compare(INinMagicShellItem other, uint hint, out int order);
}
[ComImport, Guid("947AAB5F-0A5C-4C13-B4D6-4BF7836FC9F8"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface INinMagicFileOperation {
 void Advise(IntPtr sink,out uint cookie);
 void Unadvise(uint cookie);
 void SetOperationFlags(uint flags);
 void SetProgressMessage([MarshalAs(UnmanagedType.LPWStr)] string text);
 void SetProgressDialog(IntPtr dialog);
 void SetProperties(IntPtr changes);
 void SetOwnerWindow(IntPtr owner);
 void ApplyPropertiesToItem(INinMagicShellItem item);
 void ApplyPropertiesToItems(IntPtr items);
 void RenameItem(INinMagicShellItem item,[MarshalAs(UnmanagedType.LPWStr)] string name,IntPtr sink);
 void RenameItems(IntPtr items,[MarshalAs(UnmanagedType.LPWStr)] string name);
 void MoveItem(INinMagicShellItem item,INinMagicShellItem destination,[MarshalAs(UnmanagedType.LPWStr)] string name,IntPtr sink);
 void MoveItems(IntPtr items,INinMagicShellItem destination);
 void CopyItem(INinMagicShellItem item,INinMagicShellItem destination,[MarshalAs(UnmanagedType.LPWStr)] string name,IntPtr sink);
 void CopyItems(IntPtr items,INinMagicShellItem destination);
 void DeleteItem(INinMagicShellItem item,IntPtr sink);
 void DeleteItems(IntPtr items);
 void NewItem(INinMagicShellItem destination,uint attributes,[MarshalAs(UnmanagedType.LPWStr)] string name,[MarshalAs(UnmanagedType.LPWStr)] string template,IntPtr sink);
 void PerformOperations();
 void GetAnyOperationsAborted([MarshalAs(UnmanagedType.Bool)] out bool aborted);
}
[ComVisible(true), Guid("04B0F1A7-9490-44BC-96E1-4296A31252E2"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
public interface INinMagicCopyProgress {
 [PreserveSig] int StartOperations();
 [PreserveSig] int FinishOperations(int result);
 [PreserveSig] int PreRenameItem(uint flags,IntPtr item,[MarshalAs(UnmanagedType.LPWStr)] string name);
 [PreserveSig] int PostRenameItem(uint flags,IntPtr item,[MarshalAs(UnmanagedType.LPWStr)] string name,int result,IntPtr created);
 [PreserveSig] int PreMoveItem(uint flags,IntPtr item,IntPtr destination,[MarshalAs(UnmanagedType.LPWStr)] string name);
 [PreserveSig] int PostMoveItem(uint flags,IntPtr item,IntPtr destination,[MarshalAs(UnmanagedType.LPWStr)] string name,int result,IntPtr created);
 [PreserveSig] int PreCopyItem(uint flags,IntPtr item,IntPtr destination,[MarshalAs(UnmanagedType.LPWStr)] string name);
 [PreserveSig] int PostCopyItem(uint flags,IntPtr item,IntPtr destination,[MarshalAs(UnmanagedType.LPWStr)] string name,int result,IntPtr created);
 [PreserveSig] int PreDeleteItem(uint flags,IntPtr item);
 [PreserveSig] int PostDeleteItem(uint flags,IntPtr item,int result,IntPtr created);
 [PreserveSig] int PreNewItem(uint flags,IntPtr destination,[MarshalAs(UnmanagedType.LPWStr)] string name);
 [PreserveSig] int PostNewItem(uint flags,IntPtr destination,[MarshalAs(UnmanagedType.LPWStr)] string name,[MarshalAs(UnmanagedType.LPWStr)] string template,uint attributes,int result,IntPtr created);
 [PreserveSig] int UpdateProgress(uint total,uint completed);
 [PreserveSig] int ResetTimer();
 [PreserveSig] int PauseTimer();
 [PreserveSig] int ResumeTimer();
}
[ComVisible(true), ClassInterface(ClassInterfaceType.None)]
public sealed class NinMagicCopyProgress : INinMagicCopyProgress {
 public readonly List<string> Results=new List<string>();
 public int Copies,Renames,Failure,FinishedResult;
 public int StartOperations(){return 0;}
 public int FinishOperations(int result){FinishedResult=result;if(result<0&&Failure==0)Failure=result;return 0;}
 public int PreRenameItem(uint f,IntPtr i,string n){return 0;}
 public int PostRenameItem(uint f,IntPtr i,string n,int r,IntPtr c){++Renames;Results.Add("PostRenameItem HRESULT=0x"+unchecked((uint)r).ToString("X8")+" name="+(n??""));if(r<0&&Failure==0)Failure=r;return 0;}
 public int PreMoveItem(uint f,IntPtr i,IntPtr d,string n){return 0;}
 public int PostMoveItem(uint f,IntPtr i,IntPtr d,string n,int r,IntPtr c){return 0;}
 public int PreCopyItem(uint f,IntPtr i,IntPtr d,string n){return 0;}
 public int PostCopyItem(uint f,IntPtr i,IntPtr d,string n,int r,IntPtr c){++Copies;Results.Add("PostCopyItem HRESULT=0x"+unchecked((uint)r).ToString("X8")+" created="+(c!=IntPtr.Zero)+" name="+(n??""));if(r<0&&Failure==0)Failure=r;return 0;}
 public int PreDeleteItem(uint f,IntPtr i){return 0;}
 public int PostDeleteItem(uint f,IntPtr i,int r,IntPtr c){return 0;}
 public int PreNewItem(uint f,IntPtr d,string n){return 0;}
 public int PostNewItem(uint f,IntPtr d,string n,string t,uint a,int r,IntPtr c){return 0;}
 public int UpdateProgress(uint t,uint c){return 0;}
 public int ResetTimer(){return 0;}
 public int PauseTimer(){return 0;}
 public int ResumeTimer(){return 0;}
}
public sealed class NinMagicCopyResult {
 public int CopyCallbacks;
 public bool Aborted;
 public string[] ItemResults;
}
public static class NinMagicMtpOps {
 [DllImport("shell32.dll",PreserveSig=false)]
 static extern void SHGetIDListFromObject([MarshalAs(UnmanagedType.IUnknown)] object source,out IntPtr pidl);
 [DllImport("shell32.dll",PreserveSig=false)]
 static extern void SHCreateItemFromIDList(IntPtr pidl,ref Guid iid,[MarshalAs(UnmanagedType.Interface)] out INinMagicShellItem item);
 static INinMagicShellItem ShellItem(object source) {
  if(source==null)throw new ArgumentNullException("source");
  Guid iid=typeof(INinMagicShellItem).GUID;INinMagicShellItem item;IntPtr pidl;
  SHGetIDListFromObject(source,out pidl);
  try {SHCreateItemFromIDList(pidl,ref iid,out item);}finally {Marshal.FreeCoTaskMem(pidl);}
  return item;
 }
 // destination is a FolderItem for the destination folder (Folder.Self also works).
 // This does not replace caller inventory/hash verification of the resulting copy.
 public static NinMagicCopyResult CopyChecked(object source,object destination) {
  INinMagicShellItem item=null,target=null;INinMagicFileOperation op=null;IntPtr sinkPointer=IntPtr.Zero;
  var sink=new NinMagicCopyProgress();
  try {
   item=ShellItem(source);target=ShellItem(destination);
   op=(INinMagicFileOperation)Activator.CreateInstance(Type.GetTypeFromCLSID(new Guid("3AD05575-8857-4850-9277-11B85BDB8E09")));
   // Suppress UI, do not copy security metadata to MTP, and stop at the first error.
   op.SetOperationFlags(0x4|0x10|0x200|0x400|0x800|0x100000);
   sinkPointer=Marshal.GetComInterfaceForObject(sink,typeof(INinMagicCopyProgress));
   op.CopyItem(item,target,null,sinkPointer);op.PerformOperations();
   bool aborted;op.GetAnyOperationsAborted(out aborted);
   string details=String.Join("; ",sink.Results.ToArray());
   if(sink.Failure<0)throw new COMException("MTP copy failed. "+details,sink.Failure);
   if(aborted)throw new InvalidOperationException("MTP copy was aborted. "+details);
   if(sink.Copies==0)throw new InvalidOperationException("MTP copy completed without a PostCopyItem result; no transfer was confirmed.");
   return new NinMagicCopyResult {CopyCallbacks=sink.Copies,Aborted=aborted,ItemResults=sink.Results.ToArray()};
  } catch(COMException e) {throw new COMException("CopyChecked HRESULT=0x"+unchecked((uint)e.ErrorCode).ToString("X8")+": "+e.Message,e.ErrorCode);}
  finally {
   if(sinkPointer!=IntPtr.Zero)Marshal.Release(sinkPointer);
   if(op!=null)Marshal.ReleaseComObject(op);
   if(target!=null)Marshal.ReleaseComObject(target);
   if(item!=null)Marshal.ReleaseComObject(item);
   GC.KeepAlive(sink);
  }
 }
 public static void RenameChecked(object source,string name) {
  if(String.IsNullOrWhiteSpace(name)||name.IndexOfAny(new char[]{'/', '\\', ':'})>=0||name=="."||name=="..")throw new ArgumentException("Invalid rename leaf");
  INinMagicShellItem item=null;INinMagicFileOperation op=null;IntPtr sinkPointer=IntPtr.Zero;var sink=new NinMagicCopyProgress();
  try {
   item=ShellItem(source);op=(INinMagicFileOperation)Activator.CreateInstance(Type.GetTypeFromCLSID(new Guid("3AD05575-8857-4850-9277-11B85BDB8E09")));
   op.SetOperationFlags(0x4|0x10|0x200|0x400|0x800|0x100000);sinkPointer=Marshal.GetComInterfaceForObject(sink,typeof(INinMagicCopyProgress));
   op.RenameItem(item,name,sinkPointer);op.PerformOperations();bool aborted;op.GetAnyOperationsAborted(out aborted);
   if(sink.Failure<0)throw new COMException("MTP rename failed",sink.Failure);
   if(aborted||sink.Renames!=1)throw new InvalidOperationException("MTP rename was not confirmed");
  } finally {if(sinkPointer!=IntPtr.Zero)Marshal.Release(sinkPointer);if(op!=null)Marshal.ReleaseComObject(op);if(item!=null)Marshal.ReleaseComObject(item);GC.KeepAlive(sink);}
 }
 public static void Delete(object source) {
  Guid iid=typeof(INinMagicShellItem).GUID; INinMagicShellItem item;
  IntPtr pidl;SHGetIDListFromObject(source,out pidl);
  try { SHCreateItemFromIDList(pidl,ref iid,out item); } finally {Marshal.FreeCoTaskMem(pidl);}
  var op=(INinMagicFileOperation)Activator.CreateInstance(Type.GetTypeFromCLSID(new Guid("3AD05575-8857-4850-9277-11B85BDB8E09")));
  try { op.SetOperationFlags(0x4|0x10|0x200|0x400|0x100000); op.DeleteItem(item,IntPtr.Zero); op.PerformOperations(); bool aborted;op.GetAnyOperationsAborted(out aborted);if(aborted)throw new InvalidOperationException("MTP deletion was aborted."); }
  finally { Marshal.ReleaseComObject(op);Marshal.ReleaseComObject(item); }
 }
}
'@
}
function Remove-VerifiedMtpItem($item,$parent,[string[]]$allowedNames) {
    if($allowedNames -notcontains $item.Name){throw 'La eliminación MTP no pertenece a la lista autorizada.'}
    if(-not $item.Path.StartsWith($parent.Path+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'La ruta MTP no está dentro de la carpeta de aplicación comprobada.'}
    $identity=$item.Path
    [NinMagicMtpOps]::Delete($item)
    if(@($parent.GetFolder.Items() | Where-Object {$_.Path -eq $identity}).Count -ne 0){throw 'El archivo MTP sigue presente después de eliminarlo.'}
}
