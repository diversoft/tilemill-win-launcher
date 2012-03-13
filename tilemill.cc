#include <windows.h>
#include <winbase.h>
#include <tchar.h>
#include <strsafe.h>
#include <stdio.h> 
#include <Shlobj.h> // for getting user path
#include <string> 

#define BUFSIZE 4096 
 
HANDLE g_hChildStd_IN_Rd = NULL;
HANDLE g_hChildStd_IN_Wr = NULL;
HANDLE g_hChildStd_OUT_Rd = NULL;
HANDLE g_hChildStd_OUT_Wr = NULL;
HANDLE g_hInputFile = NULL;

void ErrorExit(LPTSTR lpszFunction, DWORD dw) 
{ 
    // Retrieve the system error message for the last-error code
    LPVOID lpMsgBuf;
    LPVOID lpDisplayBuf;
    FormatMessage(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | 
        FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,
        dw,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPTSTR) &lpMsgBuf,
        0, NULL );

    // Display the error message and exit the process

    lpDisplayBuf = (LPVOID)LocalAlloc(LMEM_ZEROINIT, 
        (lstrlen((LPCTSTR)lpMsgBuf) + lstrlen((LPCTSTR)lpszFunction) + 100) * sizeof(TCHAR)); 
    StringCchPrintf((LPTSTR)lpDisplayBuf, 
        LocalSize(lpDisplayBuf) / sizeof(TCHAR),
        TEXT("%s\n\n Failed with error %d: %s\nPlease report this problem to https://github.com/mapbox/tilemill/issues"), 
        lpszFunction, dw, lpMsgBuf); 
    MessageBox(NULL, (LPCTSTR)lpDisplayBuf, TEXT("TileMill Error"), MB_OK|MB_SYSTEMMODAL); 

    LocalFree(lpMsgBuf);
    LocalFree(lpDisplayBuf);
    ExitProcess(dw); 
}

void ErrorExit(LPTSTR lpszFunction)
{
    ErrorExit(lpszFunction,GetLastError());
}

bool writeToLog(const char* chBuf)
{
    DWORD dwRead = strlen(chBuf); 
    DWORD dwWritten(0); 
    BOOL bSuccess = FALSE;

    return WriteFile(g_hInputFile, chBuf, 
                           dwRead, &dwWritten, NULL);
}

void ReadFromPipe(void) 

// Read output from the child process's pipe for STDOUT
// and write to the parent process's pipe for STDOUT. 
// Stop when there is no more data. 
{ 
   DWORD dwRead, dwWritten; 
   CHAR chBuf[BUFSIZE]; 
   BOOL bSuccess = FALSE;
   HANDLE hParentStdOut = GetStdHandle(STD_OUTPUT_HANDLE);

   // Close the write end of the pipe before reading from the 
   // read end of the pipe, to control child process execution.
   // The pipe is assumed to have enough buffer space to hold the
   // data the child process has already written to it.
 
   if (!CloseHandle(g_hChildStd_OUT_Wr)) 
      ErrorExit(TEXT("StdOutWr CloseHandle")); 
 
   for (;;) 
   { 
      bSuccess = ReadFile( g_hChildStd_OUT_Rd, chBuf, BUFSIZE, &dwRead, NULL);
	  if( ! bSuccess ) break;
      std::string debug_line(chBuf);
	  std::string substring = debug_line.substr(0,static_cast<size_t>(dwRead));
	  substring += "\nPlease report this to https://github.com/mapbox/tilemill/issues\n";
	  if (substring.find("Error:") !=std::string::npos)
	  {
		  if (substring.find("EADDRINUSE") !=std::string::npos)
		  {
		      MessageBox(NULL, static_cast<LPCSTR>("TileMill port already in use. Please quit the other application using port 20009 and then restart TileMill"), TEXT("TileMill Error"), MB_OK|MB_SYSTEMMODAL);
		  }
		  else
		  {
		      MessageBox(NULL, static_cast<LPCSTR>(substring.c_str()), TEXT("TileMill Error"), MB_OK|MB_SYSTEMMODAL);
		  }
		  ExitProcess(1);
	  }
	  bSuccess = WriteFile(g_hInputFile, chBuf, 
                           dwRead, &dwWritten, NULL);
      if (! bSuccess ) break;
      	  
   }
}

void msgExit(LPTSTR message)
{
    MessageBox(NULL, message, TEXT("TileMill Error"), MB_OK|MB_SYSTEMMODAL);
	ExitProcess(1);
}

void CreateChildProcess(TCHAR * szCmdline)
// Create a child process that uses the previously created pipes for STDIN and STDOUT.
{ 
   PROCESS_INFORMATION piProcInfo; 
   STARTUPINFO siStartInfo;
   BOOL bSuccess = FALSE; 
 
// Set up members of the PROCESS_INFORMATION structure. 
 
   ZeroMemory( &piProcInfo, sizeof(PROCESS_INFORMATION) );
 
// Set up members of the STARTUPINFO structure. 
// This structure specifies the STDIN and STDOUT handles for redirection.
 
   ZeroMemory( &siStartInfo, sizeof(STARTUPINFO) );
   siStartInfo.cb = sizeof(STARTUPINFO); 
   siStartInfo.hStdError = g_hChildStd_OUT_Wr;
   siStartInfo.hStdOutput = g_hChildStd_OUT_Wr;
   siStartInfo.hStdInput = g_hChildStd_IN_Rd;
   siStartInfo.dwFlags |= STARTF_USESTDHANDLES;
 
// Create the child process. 
    
   bSuccess = CreateProcess(NULL, 
      szCmdline,     // command line 
      NULL,          // process security attributes 
      NULL,          // primary thread security attributes 
      TRUE,          // handles are inherited 
      CREATE_NO_WINDOW,             // creation flags 
      NULL,          // use parent's environment 
      NULL,          // use parent's current directory 
      &siStartInfo,  // STARTUPINFO pointer 
      &piProcInfo);  // receives PROCESS_INFORMATION 
   
   // If an error occurs, exit the application. 
   if ( ! bSuccess ) 
      ErrorExit(TEXT("Could not launch TileMill's node process"));
   else 
   {
      // Close handles to the child process and its primary thread.
      // Some applications might keep these handles to monitor the status
      // of the child process, for example. 

      CloseHandle(piProcInfo.hProcess);
      CloseHandle(piProcInfo.hThread);
   }
}

/* Return TRUE if file 'fileName' exists */
bool FileExists(const TCHAR *fileName)
{
    DWORD       fileAttr;
    fileAttr = GetFileAttributes(fileName);
    if (0xFFFFFFFF == fileAttr)
        return false;
    return true;
}

int APIENTRY WinMain(HINSTANCE hInstance,
                     HINSTANCE hPrevInstance,
                     LPSTR    lpCmdLine,
                     int       nCmdShow) {

    if (!FileExists("tilemill\\node.exe") && !FileExists("tilemill\\node_modules"))
	   msgExit(TEXT("Could not start: TileMill.exe could not find supporting files")); 
	SECURITY_ATTRIBUTES saAttr; 
 
// Set the bInheritHandle flag so pipe handles are inherited. 
 
   saAttr.nLength = sizeof(SECURITY_ATTRIBUTES); 
   saAttr.bInheritHandle = TRUE; 
   saAttr.lpSecurityDescriptor = NULL; 

// Create a pipe for the child process's STDOUT. 
 
   if ( ! CreatePipe(&g_hChildStd_OUT_Rd, &g_hChildStd_OUT_Wr, &saAttr, 0) ) 
      ErrorExit(TEXT("could not create pipe to stdout")); 

// Ensure the read handle to the pipe for STDOUT is not inherited.

   if ( ! SetHandleInformation(g_hChildStd_OUT_Rd, HANDLE_FLAG_INHERIT, 0) )
      ErrorExit(TEXT("could not get stdout handle info")); 

// Create a pipe for the child process's STDIN. 
 
   if (! CreatePipe(&g_hChildStd_IN_Rd, &g_hChildStd_IN_Wr, &saAttr, 0)) 
      ErrorExit(TEXT("could not create pipe to stdin")); 

// Ensure the write handle to the pipe for STDIN is not inherited. 
 
   if ( ! SetHandleInformation(g_hChildStd_IN_Wr, HANDLE_FLAG_INHERIT, 0) )
      ErrorExit(TEXT("could not get stdin handle info")); 

   /* 
   * Set env variable in the current process.  It'll get inherited by
   * node process.
   */
  if (!SetEnvironmentVariableA("PROJ_LIB",".\\tilemill\\data\\proj\\nad"))
      ErrorExit("TileMill.exe setting env: ",GetLastError());
  if (!SetEnvironmentVariableA("GDAL_DATA",".\\tilemill\\data\\gdal\\data"))
      ErrorExit("TileMill.exe setting env: ",GetLastError());
  if (!SetEnvironmentVariableA("PATH",".\\tilemill\\addons\\mapnik\\lib\\mapnik\\lib;.\\tilemill\\addons\\zipfile\\lib;%PATH%"))
      ErrorExit("TileMill.exe setting env: ",GetLastError());
  if (!SetEnvironmentVariableA("NODE_PATH",".\\tilemill\\addons"))
      ErrorExit("TileMill.exe setting env: ",GetLastError());

  // Create the child process.
  TCHAR cmd[]=TEXT(".\\tilemill\\node.exe .\\tilemill\\index.js");  
  CreateChildProcess(cmd);

   // String buffer for holding the path.

   TCHAR strPath[ MAX_PATH ];

   // Get the special folder path.
    SHGetSpecialFolderPath(
      0,       // Hwnd
      strPath, // String buffer.
      CSIDL_PROFILE, // CSLID of folder
      FALSE ); // Create if doesn't exists?
	
   std::string logpath(strPath);
   logpath += "\\tilemill.log";
   g_hInputFile = CreateFile(
       logpath.c_str(),
       FILE_APPEND_DATA,
       FILE_SHARE_READ,
       NULL,
       OPEN_EXISTING,
       FILE_ATTRIBUTE_NORMAL,
       NULL);

	// if it already existed then the error code will be ERROR_FILE_NOT_FOUND
	if (GetLastError() == ERROR_FILE_NOT_FOUND)
	{
       g_hInputFile = CreateFile(
         logpath.c_str(),
         FILE_APPEND_DATA,
         FILE_SHARE_READ,
         NULL,
         CREATE_ALWAYS,
         FILE_ATTRIBUTE_NORMAL,
         NULL);
	}

    if ( g_hInputFile == INVALID_HANDLE_VALUE )
	{
      std::string err_msg("Could not create the TileMill log file at: '");
	  err_msg += logpath;
	  err_msg += "'";
	  LPTSTR l_msg = (LPTSTR)(err_msg.c_str());
	  ErrorExit(l_msg); 
	}
  
   // Read from pipe that is the standard output for child process. 
   writeToLog("Starting TileMill...\n");
   ReadFromPipe(); 
   return 0;
}