/*****************************************************************************
** COMP(ML)  PROD(MAI):  MailPage - Issue a command for Phonemail
**
** COMPONENT NAME: PopCheck.c
**
** CLASSIFICATION: Copyright IBM Corp., 2002, 2021
**
** FUNCTION:
**  This is a program that will look at a pop3 mailbox for status
**
**
** Version:
**  PopCheck release 1.0.2hhhhh
**
** Dependencies:
**  This is a Win32 Program
**
** Developer: Thomas E.Link
**
** Change Activity:
**
**  PIT#      Name      Date         Abstract
**  0001      Tom Link  2012/08/14   Remove free() for non-malloced data
**  0002      Tom Link  2021/01/18   Add SetForegroundWindow before popup menu call 
**
*****************************************************************************/

/****************************************************************************\
                              Includes
\****************************************************************************/

#include <windows.h>
#include <commctrl.h>
#include <Mmsystem.h>
#include <shellapi.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <process.h>
#include <io.h>
#include <errno.h>
#include <time.h>
#include "popcheck.h"

#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "comctl32.lib")

#define SZPOS           "WindowSizeAndPosition"
#define SZPRO           "ThreadDataStructure"
#define MAILKEY         "SOFTWARE\\PopCheck"
#define fdwrite(sock,buffer,length)  send(sock,buffer,length,0)
#define fdread(sock,buffer,length)   recv(sock,buffer,length,0)
#define INITBUF 1024

typedef struct _TDATA
{
  DWORD uMax;
  DWORD uDelay;
  CHAR  szUser[256];
  CHAR  szPass[256];
  CHAR  szServer[256];
  CHAR  szSocks[256];
  CHAR  szEmail[256];
  CHAR  szSound[256];
  BOOL  bShowstat;
  BOOL  bLreset;

  HWND hwndMain; 
  HWND hwndStatus; 
  HWND hwndList; 
  DWORD index;
  int mailCnt;
  int mailChecked;
  BOOL checkNow;
  int sock;
  int alive;
} TDATA, *PTDATA;


BOOL bFirst = FALSE;
CHAR szWndClass[] = "PopCheckWnd";
CHAR szApp[] = "PopCheck";
HINSTANCE hInst;


#define PRINTL(x) \
{ \
  char szTxt[256]; \
  sprintf x;        \
  pT->index = SendMessage(pT->hwndMain,\
                          UM_DISPSTR, \
                          (WPARAM)szTxt, \
                          (LPARAM)0); \
}


/****************************************************************************\
                          Forward Declarations
\****************************************************************************/
LRESULT CALLBACK MainWndProc(HWND hwnd,UINT msg,WPARAM wParam,LPARAM lParam);
LRESULT CALLBACK AboutDlgProc(HWND hwnd,UINT msg,WPARAM wParam,LPARAM lParam);
LRESULT CALLBACK SetupDlgProc(HWND hwnd,UINT msg,WPARAM wParam,LPARAM lParam);
int getPop(PTDATA pT, int *cntmsg);
CHAR *remss( CHAR *eds);
void mailThread(PTDATA pT);
void iconThread(PTDATA pT);
void monThread(PTDATA pT);
void DrawBitmap (HDC hdc, WORD id, RECT rect, DWORD rop);
BOOL TaskBarAddIcon(HWND hwnd, UINT uID, HICON hicon, LPSTR lpszTip);
BOOL TaskBarChgIcon(HWND hwnd, UINT uID, HICON hicon);
BOOL TaskBarChgText(HWND hwnd, UINT uID, LPSTR lpszTip);
BOOL TaskBarDelIcon(HWND hwnd, UINT uID);

/*****************************************************************************
**
** Name: WinMain
**
** Abstract:
**  This function is the entry point
**
** Description:
**  Main reads the command line parameters, builds the message queue
**  and creates the main window.
**
** Parameters:
**  none
**
** Input:
**  global data
**
** Output:
**  none
**
** Return Codes:
**  0
**
*****************************************************************************/

int PASCAL WinMain(HINSTANCE hInstance,HINSTANCE hPrevInstance,LPSTR lpszCmdLine,
                   int nCmdShow)
{
  MSG msg;
  WNDCLASS wndclass;
  HMENU hMenu;
  HKEY hKey;
  DWORD flag;
  DWORD siz;
  HWND hwndFrame;
  HWND hPrev = FindWindow(szWndClass, NULL);

  if (hPrev)
  {
    /************************************************************************
    ** If this window class exists, just switch to it
    ************************************************************************/
    SetActiveWindow(hPrev);
    SetForegroundWindow(hPrev);
    return(0);
  }

  if (!hPrevInstance)
    {
      /* Register the main window */
      wndclass.style = CS_HREDRAW|CS_VREDRAW;
      wndclass.lpfnWndProc = (WNDPROC)MainWndProc;
      wndclass.cbClsExtra = 0;
      wndclass.cbWndExtra = sizeof(PVOID);
      wndclass.hInstance = hInstance;
      wndclass.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDW_MAIN));
      wndclass.hCursor = LoadCursor(NULL, IDC_ARROW);
      wndclass.hbrBackground = GetStockObject(WHITE_BRUSH);
      wndclass.lpszMenuName = NULL;
      wndclass.lpszClassName = szWndClass;
      RegisterClass(&wndclass);
    }
  hInst = hInstance;
  tzset();

  /**************************************************************************/
  /* Create the main windows                                                */
  /**************************************************************************/

  hwndFrame = CreateWindow(szWndClass,
                           szApp,
                           WS_OVERLAPPEDWINDOW,
                           CW_USEDEFAULT, CW_USEDEFAULT,
                           CW_USEDEFAULT, CW_USEDEFAULT,
                           NULL,
                           hMenu = LoadMenu(hInst, MAKEINTRESOURCE(IDW_MAIN)),
                           hInst,
                           NULL);

  TaskBarAddIcon(hwndFrame, IDW_MAIN, wndclass.hIcon, szApp);
  
  /**************************************************************************/
  /* only continue if the window create succeeded                           */
  /**************************************************************************/

  if (hwndFrame)
    {
      WINDOWPLACEMENT wpl;

      flag = REG_BINARY;
      siz = sizeof(wpl);
      if (ERROR_SUCCESS == RegOpenKey(HKEY_CURRENT_USER, MAILKEY, &hKey))
        {
          RegQueryValueEx(hKey, SZPOS, NULL, &flag, (LPBYTE)&wpl, &siz);
          RegCloseKey(hKey);
          SetWindowPlacement(hwndFrame, &wpl);
        }
      if (NULL == strstr(lpszCmdLine, "/s")) /* Hide by default */
      {
        PostMessage(hwndFrame, WM_COMMAND, IDM_HIDE, 0);
      }
      else
      {
        ShowWindow(hwndFrame, nCmdShow);
        UpdateWindow(hwndFrame);
      }

      /**********************************************************************/
      /* MAIN sits in this loop                                             */
      /**********************************************************************/

      while (GetMessage(&msg, NULL, 0, 0))
        {
          TranslateMessage(&msg);
          DispatchMessage(&msg);
        }
    }

  TaskBarDelIcon(hwndFrame, IDW_MAIN);

  /**************************************************************************/
  /* Termination cleanup                                                    */
  /**************************************************************************/
  return (0);
}

/****************************************************************************
** MOD(MainWndProc) COMP(ML)  PROD(MAI):  MailPage - Issue a command for Phonemail
**
** Name: MainWndProc
**
** Abstract:
**  Window Procedure.
**
** Description:
**  This function handles messages for the main window.
**
** Parameters:
**  Standard Window
**
** Input:
**  Globals
**
** Output:
**  same as input
**
** Return Codes:
**  Standard Window     - see each message
**
**-----------------------------------------------------------------------
** PSEUDOCODE:
** WM_CREATE
**  | Allocate instance data
**  | Init Instance Data
**  | Create input pipe
**  | Create Child windows
**  | Set default fonts (will be overridden by WinRestore in main)
**  | Start three background threads
**  | if (first time this is run)
**  |  | Post a message to display settings dialog)
**  | end
** END
** UM_DISPSTR
** UM_DISPMSG
**  |
** END
*****************************************************************************/

LRESULT CALLBACK MainWndProc(HWND hwnd,UINT msg,WPARAM wParam,LPARAM lParam)
{
  DWORD flag, siz;
  HKEY hKey;
  SECURITY_ATTRIBUTES sa = {0};
  WINDOWPLACEMENT wpl;
  RECT rect;
  int aStat[2];
  static PTDATA pT;
  static BOOL bDlg;
  static BOOL bHidden;
  switch (msg)
    {
      case  WM_CREATE :

        pT = (PTDATA)malloc(sizeof(TDATA));
        memset(pT, 0, sizeof(TDATA));
        bDlg = FALSE;
        bHidden = FALSE;

        /********************************************************************/
        /* Start by getting the profile info                                */
        /********************************************************************/

        flag = REG_BINARY;
        siz = sizeof(TDATA);
        if (ERROR_SUCCESS == RegOpenKey(HKEY_CURRENT_USER, MAILKEY, &hKey))
        {
          RegQueryValueEx(hKey, SZPRO, NULL, &flag, (LPBYTE)pT, &siz);
          RegCloseKey(hKey);
        }
        else
        {
          pT->uMax = 50;
          pT->uDelay = 300; /* 300 seconds = 5 minutes */
          pT->bShowstat = TRUE;
          pT->bLreset = FALSE;
          bFirst = TRUE;
        }
        pT->index = 0;
        pT->mailCnt = 0;
        pT->mailChecked = 0;
        pT->checkNow = FALSE;
        pT->sock = 0;
        pT->alive = 0;

        /********************************************************************/
        /* Create the LISTBOX and STATUS windows                            */
        /********************************************************************/

        pT->hwndMain = hwnd;
        GetClientRect(hwnd, &rect);
        pT->hwndList = CreateWindow("LISTBOX",
                                 "",
                                 WS_VISIBLE|LBS_HASSTRINGS|LBS_DISABLENOSCROLL|
                                 LBS_NOSEL|WS_CHILD|WS_CLIPSIBLINGS|WS_VSCROLL,
                                 rect.left,rect.top,rect.right,rect.bottom,
                                 hwnd,
                                 (HMENU)IDW_LIST,
                                 hInst,
                                 NULL);
        if (pT->hwndList == NULL)
          MessageBox (NULL, "List not created!", NULL, MB_OK );
        pT->hwndStatus = (HWND)CreateStatusWindow( WS_CHILD | WS_BORDER | WS_VISIBLE,
                                            "",
                                            hwnd,
                                            IDW_STATUS);
        if (pT->hwndStatus == NULL)
          MessageBox (NULL, "Status Bar not created!", NULL, MB_OK );

        /********************************************************************/
        /* Start the threads for port and pipe                              */
        /********************************************************************/

        _beginthread(mailThread, 0x4000, (PVOID)pT);
        _beginthread(iconThread, 0x4000, (PVOID)pT);
        _beginthread(monThread, 0x4000, (PVOID)pT);

        /********************************************************************/
        /* one item to start                                                */
        /********************************************************************/

        pT->index = SendMessage(pT->hwndList,
                                LB_ADDSTRING,
                                0,
                                (LPARAM)"");

        /********************************************************************/
        /* If nothing is set up, invoke setup                               */
        /********************************************************************/

        if (bFirst)
          PostMessage(hwnd,
                      WM_COMMAND,
                      IDM_SETUP,
                      (LPARAM)0);
        break;
      case WM_KEYDOWN:
        //if (wParam == VK_F1)
          // XhCallIPF(hwndHelp, HM_DISPLAY_HELP,
        break;
      case  UM_DISPSTR :

        /********************************************************************/
        /* string is passed in, put in listbox                              */
        /********************************************************************/

        remss((CHAR *)wParam);
        pT->index = SendMessage(pT->hwndList,
                                LB_ADDSTRING,
                                0,
                                (LPARAM)wParam);
        SendMessage(pT->hwndList,
                    LB_SETCURSEL,
                    pT->index,
                    (LPARAM)wParam);

        /********************************************************************/
        /* Limit listbox entries                                            */
        /********************************************************************/

        if (pT->index >= pT->uMax)
          SendMessage(pT->hwndList,
                      LB_DELETESTRING,
                      0,
                      0);
        break;
    case  UM_MESSAGE :
      switch (lParam)
      {
        case WM_RBUTTONUP:
          {     /* Single right click displays menu */
            HMENU hMen = LoadMenu(hInst, MAKEINTRESOURCE(IDW_MAIN2));
            HMENU hPop = GetSubMenu(hMen, 0);
            POINT pnt;
            GetCursorPos(&pnt);
            if (pnt.x <= 4100)
              SetForegroundWindow(hwnd);
              TrackPopupMenu(hPop,  
               0,    
               pnt.x, 
               pnt.y,
               0,  
               hwnd, 
               NULL);
            DestroyMenu(hMen);
          }
          break;
        case WM_LBUTTONDBLCLK:
          if (strlen(pT->szEmail))
          { /* Double Left click starts mail */
            ShellExecute(NULL,
                         "open",
                          pT->szEmail,
                          NULL,
                          NULL,
                          SW_SHOWDEFAULT);
            if (pT->bLreset)
            {
              pT->mailChecked = pT->mailCnt;
            }
          }
          break;
        default: 
          break;
      } /* end switch */
      break;
    case  UM_DISPMSG :

        /********************************************************************/
        /* id of string is passed in, put in listbox                        */
        /********************************************************************/

        pT->index = SendMessage(pT->hwndList,
                                LB_ADDSTRING,
                                0,
                                (LPARAM)lParam);
        SendMessage(pT->hwndList,
                    LB_SETCURSEL,
                    pT->index,
                    (LPARAM)wParam);

        /********************************************************************/
        /* Limit listbox entries                                            */
        /********************************************************************/

        if (pT->index >= pT->uMax)
          SendMessage(pT->hwndList,
                      LB_DELETESTRING,
                      0,
                      0);
        break;
      case  WM_SIZE :

        /********************************************************************/
        /* Size the children                                                */
        /********************************************************************/

        if (NULL == pT->hwndList)
          break;
        SetWindowPos(pT->hwndStatus,
                     HWND_TOP,
                     0,HIWORD(lParam)-(pT->bShowstat?
                       (2*LOWORD(GetSystemMetrics(SM_CYCAPTION))):0),
                     LOWORD(lParam),
                     (pT->bShowstat?
                       (2*LOWORD(GetSystemMetrics(SM_CYCAPTION))):0),
                     (pT->bShowstat?
                       SWP_SHOWWINDOW:SWP_HIDEWINDOW));
        SetWindowPos(pT->hwndList,
                     HWND_TOP,
                     0,0,LOWORD(lParam),HIWORD(lParam)-
                     (pT->bShowstat?
                       LOWORD(GetSystemMetrics(SM_CYCAPTION)):0),
                     SWP_SHOWWINDOW);
        aStat[0] = (LOWORD(lParam)*1)/2;  /* First side 1/2 width */
        aStat[1] = -1;
        SendMessage(pT->hwndStatus,
                    SB_SETPARTS,
                    2,
                    (LPARAM)&aStat);
        break;
      case  WM_COMMAND :
        switch (LOWORD(wParam))
          {
            case  IDM_SETUP :
              if (bHidden)
              {
                SetWindowText(hwnd, szApp);
                SetForegroundWindow(hwnd);
              }
              if (!bDlg)
              {
                bDlg = TRUE;
                if (DialogBoxParam(hInst,
                                   MAKEINTRESOURCE(DLG_SETUP),
                                   hwnd,
                                   (DLGPROC)SetupDlgProc,
                                   (LPARAM)pT))
                  {
                    RECT rect;

                    /**********************************************************/
                    /* Change the children size in case options changed       */
                    /**********************************************************/

                    GetClientRect(hwnd, &rect);
                    PostMessage(hwnd,
                               WM_SIZE,
                               (ULONG)0,
                               MAKELONG(rect.right - rect.left,
                                            rect.bottom -rect.top));

                    /**********************************************************/
                    /* Adjust the list for new count                          */
                    /**********************************************************/

                    while ((UINT)SendMessage(pT->hwndList, LB_GETCOUNT,0,0)
                            > pT->uMax)
                      SendMessage(pT->hwndList,
                                 LB_DELETESTRING,
                                 0,0);


                    /**********************************************************/
                    /* Save the profile                                       */
                    /**********************************************************/

                    if (ERROR_SUCCESS == RegCreateKey(HKEY_CURRENT_USER,
                                                      MAILKEY, &hKey))
                      {
                        RegSetValueEx(hKey, SZPRO, 0, REG_BINARY, (LPBYTE)pT,
                                      sizeof(TDATA));
                        RegCloseKey(hKey);
                      }
                  }
                if (bHidden)
                {
                  PostMessage(hwnd, WM_COMMAND, IDM_HIDE, 0);
                }
                bDlg = FALSE;
              }
              break;
            case  IDM_HIDE:
              bHidden = TRUE;
              ShowWindow(hwnd, SW_HIDE);
              SetWindowText(hwnd, "");
              SetFocus( GetWindow(hwnd,
                                  GW_HWNDNEXT));
              break;
            case  IDM_SHOW:
              bHidden = FALSE;
              ShowWindow(hwnd, SW_SHOW);
              SetWindowText(hwnd, szApp);
              SetForegroundWindow(hwnd);
              break;
            case  IDM_EMAIL:
              PostMessage(hwnd, UM_MESSAGE, 0, WM_LBUTTONDBLCLK);
              break;
            case  IDM_CHECKNOW:
              pT->checkNow = TRUE;
              break;
            case  IDM_ABOUT :
              if (bHidden)
              {
                SetWindowText(hwnd, szApp);
                SetForegroundWindow(hwnd);
              }
              if (!bDlg)
              {
                bDlg = TRUE;
                DialogBox(hInst,
                          MAKEINTRESOURCE(DLG_ABOUT),
                          hwnd,
                          (DLGPROC)AboutDlgProc);
                if (bHidden)
                {
                  PostMessage(hwnd, WM_COMMAND, IDM_HIDE, 0);
                }
                bDlg = FALSE;
              }
              break;
            case  IDM_EXIT :
              GetWindowPlacement(hwnd, &wpl);
              if (ERROR_SUCCESS == RegCreateKey(HKEY_CURRENT_USER,
                                                MAILKEY, &hKey))
                {
                  RegSetValueEx(hKey, SZPOS, 0, REG_BINARY, (LPBYTE)&wpl,
                                sizeof(wpl));
                  RegCloseKey(hKey);
                }

              PostQuitMessage(0);
              break;
            default  :
#ifdef PDEBUG
              sprintf(szTxt, "Message number %u received", wParam);
              MessageBox(   hwndFrame,
                            (PSZ)szTxt,
                            (PSZ)szApp,
                            MB_OK);
#endif
              break;
          }                       /* endswitch                              */
        break;
      case  WM_CLOSE :
        PostMessage(hwnd, WM_COMMAND, IDM_HIDE, 0);
        break;
      default  :
        return(DefWindowProc(hwnd,
                             msg,
                             wParam,
                             lParam));
    }
  return  (0);
}


/*****************************************************************************
**
** Name: AboutDlgProc
**
** Abstract:
**  Window Procedure.
**
** Description:
**  Display the ABOUT dialog box.
**
** Input:
**  resources with About version string
**
** Output:
**  to screen
**
** Return Codes:
**  none
**
*****************************************************************************/

LRESULT CALLBACK AboutDlgProc(HWND hwnd,UINT msg,WPARAM wParam,LPARAM lParam)
{
  RECT rcl;
  HDC hdc;
  HWND hwndRect;

  switch (msg)
    {
      case  WM_INITDIALOG :
        return(TRUE);
      case  WM_PAINT :
        PostMessage(hwnd, UM_DRAWBITMAP, wParam, lParam);
        break;
      case  WM_CLOSE :
      case  WM_COMMAND :
         KillTimer(hwnd, 1);
         EndDialog(hwnd, TRUE);
         break;
      case  UM_DRAWBITMAP :

        /********************************************************************/
        /* draw the bitmap for the IBM logo                                 */
        /********************************************************************/

        //hdc = GetDC(hwnd);                     /* Draw on client            */
        hwndRect = GetDlgItem(hwnd,            /* Find hidden rect          */
                              ID_RECT);
        hdc = GetDC(hwndRect);                     /* Draw on client            */
        GetWindowRect(hwndRect,
                      &rcl);
        MapWindowPoints(NULL, hwnd, (LPPOINT)&rcl, 2); /* Map to client     */
        DrawBitmap(hdc, IDB_IBML, rcl, SRCAND);
        ReleaseDC(hwnd, hdc);        /* Drawing is complete                 */
        break;
    }
  return ((DWORD)NULL);
}


/*****************************************************************************
** MOD(SetupDlgProc) COMP(ML)  PROD(MAI):  MailPage - Issue a command for Phonemail
**
** Name: SetupDlgProc
**
** Abstract:
**  Window Procedure.
**
** Description:
**  Window procedure for the PORT options dialog.
**
** Parameters:
**  standard window
**
** Input:
**  pT - pointer to a thread data structure comes in on lParam.
**
** Output:
**  to input structure
**
** Return Codes:
**  standard window
**
**-----------------------------------------------------------------------
** PSEUDOCODE:
** END
*********************************************************************/

LRESULT CALLBACK SetupDlgProc(HWND hwnd,UINT msg,WPARAM wParam,LPARAM lParam)
{
  BOOL bRc;
  static PTDATA pT;

  switch (msg)
    {
      case  WM_INITDIALOG :

        pT = (PTDATA)lParam;

        /********************************************************************/
        /* Init the text fields                                             */
        /********************************************************************/

        SetDlgItemInt(hwnd,
                      ID_EDT_LWAIT,
                      (UINT)pT->uDelay,
                      FALSE);
        SetDlgItemInt(hwnd,
                      ID_EDT_UMAX,
                      (UINT)pT->uMax,
                      FALSE);

        /********************************************************************/
        /* Limit text in the entry fields                                   */
        /********************************************************************/

        SendDlgItemMessage(hwnd,
                          ID_EDT_USER,
                          EM_LIMITTEXT,
                          (WPARAM)sizeof(pT->szUser),
                          0);
        SendDlgItemMessage(hwnd,
                          ID_EDT_PASS,
                          EM_LIMITTEXT,
                          (WPARAM)sizeof(pT->szPass),
                          0);
        SendDlgItemMessage(hwnd,
                          ID_EDT_SERVER,
                          EM_LIMITTEXT,
                          (WPARAM)sizeof(pT->szServer),
                          0);
        SendDlgItemMessage(hwnd,
                          ID_EDT_SOCKS,
                          EM_LIMITTEXT,
                          (WPARAM)sizeof(pT->szSocks),
                          0);
        SendDlgItemMessage(hwnd,
                          ID_EDT_EMAIL,
                          EM_LIMITTEXT,
                          (WPARAM)sizeof(pT->szEmail),
                          0);
        SendDlgItemMessage(hwnd,
                          ID_EDT_SOUND,
                          EM_LIMITTEXT,
                          (WPARAM)sizeof(pT->szSound),
                          0);

        /********************************************************************/
        /* And init the text                                                */
        /********************************************************************/

        SetDlgItemText(hwnd,
                       ID_EDT_USER,
                       pT->szUser);
        SetDlgItemText(hwnd,
                       ID_EDT_PASS,
                       pT->szPass);
        SetDlgItemText(hwnd,
                       ID_EDT_SERVER,
                       pT->szServer);
        SetDlgItemText(hwnd,
                       ID_EDT_SOCKS,
                       pT->szSocks);
        SetDlgItemText(hwnd,
                       ID_EDT_EMAIL,
                       pT->szEmail);
        SetDlgItemText(hwnd,
                       ID_EDT_SOUND,
                       pT->szSound);

        CheckDlgButton(hwnd,
                       ID_CHK_SHOWSTAT,
                       pT->bShowstat);
        CheckDlgButton(hwnd,
                       ID_CHK_LRESET,
                       pT->bLreset);

        bFirst = FALSE;
        return(TRUE);
      case  WM_COMMAND :
        switch (LOWORD(wParam))
          {
            case  IDOK :

              /**************************************************************/
              /* Get the delay values                                       */
              /**************************************************************/

              pT->uDelay = GetDlgItemInt(hwnd,
                                  ID_EDT_LWAIT,
                                  &bRc,
                                  FALSE);
              pT->uMax = GetDlgItemInt(hwnd,
                                  ID_EDT_UMAX,
                                  &bRc,
                                  FALSE);


              /**************************************************************/
              /* get the commands                                           */
              /**************************************************************/

              GetDlgItemText(hwnd,
                                  ID_EDT_USER,
                                  pT->szUser,
                                  sizeof(pT->szUser));
              remss(pT->szUser);
              GetDlgItemText(hwnd,
                                  ID_EDT_PASS,
                                  pT->szPass,
                                  sizeof(pT->szPass));
              remss(pT->szPass);
              GetDlgItemText(hwnd,
                                  ID_EDT_SERVER,
                                  pT->szServer,
                                  sizeof(pT->szServer));
              remss(pT->szServer);
              GetDlgItemText(hwnd,
                                  ID_EDT_SOCKS,
                                  pT->szSocks,
                                  sizeof(pT->szSocks));
              remss(pT->szSocks);
              GetDlgItemText(hwnd,
                                  ID_EDT_EMAIL,
                                  pT->szEmail,
                                  sizeof(pT->szEmail));
              remss(pT->szEmail);
              GetDlgItemText(hwnd,
                                  ID_EDT_SOUND,
                                  pT->szSound,
                                  sizeof(pT->szSound));
              remss(pT->szSound);

              /**************************************************************/
              /* Get the retry checkboxes and the status window checkbox    */
              /**************************************************************/

              pT->bShowstat = IsDlgButtonChecked(hwnd, ID_CHK_SHOWSTAT);
              pT->bLreset = IsDlgButtonChecked(hwnd, ID_CHK_LRESET);
              EndDialog(hwnd, TRUE);
              break;

            case  DID_BROWSEE :
              /* Get a file name. */
              {
                OPENFILENAME ofn = {0};
                CHAR szFilter[MAX_PATH]="All Files\0*.*\0Program Files\0*.exe;*.bat;*.cmd;*.lnk\0\0";
                CHAR szTxt[MAX_PATH];

                GetDlgItemText(hwnd,
                                    ID_EDT_EMAIL,
                                    szTxt,
                                    sizeof(szTxt));
                remss(szTxt);
                ofn.lStructSize = sizeof(OPENFILENAME);
                ofn.hwndOwner = hwnd;
                ofn.lpstrFilter = szFilter;
                ofn.nFilterIndex = 2;
                ofn.lpstrFile= szTxt;
                ofn.nMaxFile = sizeof(szTxt);
                ofn.lpstrFileTitle = NULL;
                ofn.nMaxFileTitle = 0;
                ofn.lpstrInitialDir = NULL;
                ofn.Flags = OFN_SHOWHELP | OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NODEREFERENCELINKS ;

                if (GetOpenFileName(&ofn))
                {
                  SetDlgItemText(hwnd,
                                 ID_EDT_EMAIL,
                                 szTxt);
                }
              }
              break;
            case  DID_BROWSES :
              /* Get a file name. */
              {
                OPENFILENAME ofn = {0};
                CHAR szFilter[MAX_PATH]="All Files\0*.*\0Sound Files\0*.wav;*.mid\0\0";
                CHAR szTxt[MAX_PATH];

                GetDlgItemText(hwnd,
                                    ID_EDT_SOUND,
                                    szTxt,
                                    sizeof(szTxt));
                remss(szTxt);
                ofn.lStructSize = sizeof(OPENFILENAME);
                ofn.hwndOwner = hwnd;
                ofn.lpstrFilter = szFilter;
                ofn.nFilterIndex = 2;
                ofn.lpstrFile= szTxt;
                ofn.nMaxFile = sizeof(szTxt);
                ofn.lpstrFileTitle = NULL;
                ofn.nMaxFileTitle = 0;
                ofn.lpstrInitialDir = NULL;
                ofn.Flags = OFN_SHOWHELP | OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NODEREFERENCELINKS ;

                if (GetOpenFileName(&ofn))
                {
                  SetDlgItemText(hwnd,
                                 ID_EDT_SOUND,
                                 szTxt);
                }
              }
              break;

            case  IDCANCEL :

              /**************************************************************/
              /* Save nothing                                               */
              /**************************************************************/

              EndDialog(hwnd, FALSE);
              break;
            case DID_HELP:
              PostMessage(hwnd, UM_HELP, 0, 0);
              break;
          }
        break;
    }
  return (0);
}


/*****************************************************************************
**
** Name: getPop
**
** Abstract:
**  Log onto mail server and get message count
**
** Description:
**  Use passed in data structure with userid and password
**
** Parameters:
**  pT - TDATA structure
**  pcntmsg - pointer to int with message count
**
** Input:
**  See Parameters
**
** Output:
**  to pcntmsg
**
** Return Codes:
**  0 - all went OK
**  ? - Failing return code
**
*********************************************************************/

int getPop(PTDATA pT, int *pcntmsg)
{
  int    index,n;
  struct sockaddr_in addr;
  struct hostent * hostent;
  char   *p;
  int     chunk = INITBUF;
  char   *service=(char *)malloc(chunk+1);
  char   proxyhost[128];
  int    streamtype = SOCK_STREAM;
  USHORT proxyport=1080;
  USHORT port = 110;
  int    rc;
  int    state = 0;
  int    msgsiz = 0;
  unsigned char data[20];
  int err;
  WORD               wVersion;
  WSADATA            wsaData = { 0 };

  _strtime( data );
  PRINTL((szTxt, "%s - Entering getPop.", data));

  /* Windows Socket init */
  wVersion = MAKEWORD(1,1);
  err = WSAStartup( wVersion, &wsaData );

  if (err)
  {
    PRINTL((szTxt,"Error starting Windows Sockets.  Error: %d",err));
    return err;
  }

  PRINTL((szTxt,"Init OK."));
  PRINTL((szTxt,"Server=%s", pT->szServer));
  if (strlen(pT->szSocks))
  {
    proxyport=1080;
    if ((p=strchr(pT->szSocks,':'))!=(char *)0)
    {
      sscanf(p+1,"%d",&proxyport);
      *p=0;
    }
    strcpy(proxyhost,pT->szSocks);

    if ( (hostent= gethostbyname(proxyhost))==(struct hostent *)0)
    {
      rc=WSAGetLastError();
      PRINTL((szTxt,"Host proxy [%s:%d] unknown errno=%d", proxyhost, proxyport, rc));
      WSACleanup();
      return rc;
    }


    if ( (pT->sock=socket(AF_INET, SOCK_STREAM,0))<0)
    {      /*CONNECT to Server */
      rc=WSAGetLastError();
      PRINTL((szTxt,"Proxy Socket(): errno = %d",rc));
      WSACleanup();
      return rc;
    }

    memset(&addr, 0, sizeof(addr) );
    addr.sin_family = AF_INET;


    memcpy((char *)&addr.sin_addr, (char *)hostent->h_addr, hostent->h_length);
    addr.sin_port = htons(proxyport);

    if ( connect(pT->sock,(struct sockaddr *) &addr, sizeof(addr))<0)
    {
      rc=WSAGetLastError();
      PRINTL((szTxt,"connect() %s@%d not running.",proxyhost,port));
      PRINTL((szTxt,"connect() failed errno=%d ",rc));
      WSACleanup();
      return rc;

    }

    data[0] = 4;                   // Socks version (4)
    data[1] = 1;                   // Socks command 1, CONNECT
    data[2] = (unsigned char) ((port & 0xff00) >> 8); // port, high byte
    data[3] = (unsigned char) (port & 0xff); // port, low byte

    if ( (hostent= gethostbyname(pT->szServer))==(struct hostent *)0)
    {
      rc=WSAGetLastError();
      PRINTL((szTxt,"Host invalid [%s]",pT->szServer));
      PRINTL((szTxt,"Host unknown err=%d",rc));
      WSACleanup();
      return rc;
    }
    memcpy(data+4, (char *)hostent->h_addr, hostent->h_length);
    strcpy((char *) (data + 8), "anonymous");
    if (18!=fdwrite(pT->sock,data,18))
    {
      rc=GetLastError();
      PRINTL((szTxt,"proxy write failed errno=%d ",rc));
      WSACleanup();
      return rc;
    }

    if (8 != fdread(pT->sock, (char *) data,8))
    {
      rc=GetLastError();
      PRINTL((szTxt,"proxy read failed errno=%d ",rc));
      closesocket(pT->sock);
      pT->sock = 0;
      WSACleanup();
      return rc;
    }
    if (data[0] != 0 || data[1] != 90)
    {
      int v1 = data[0], v2 = data[1];
      rc=GetLastError();
      PRINTL((szTxt,"proxy read failed errno=%d ",rc));
      closesocket(pT->sock);
      pT->sock = 0;
      WSACleanup();
      return rc;
    }
  } 
  else /* Direct connect, no proxy */
  {
    PRINTL((szTxt, "No proxy"));
    if ( (hostent= gethostbyname(pT->szServer))==(struct hostent *)0)
    {
      rc=WSAGetLastError();
      PRINTL((szTxt,"Host invalid -[%s]",pT->szServer));
      PRINTL((szTxt,"Host unknown err=%d",rc));
      WSACleanup();
      return rc;
    }


    if ( (pT->sock=socket(AF_INET, SOCK_STREAM,0))<0)
    {      /*CONNECT to Server */
      rc=WSAGetLastError();
      PRINTL((szTxt,"Socket(): errno = %d ",rc));
      WSACleanup();
      return rc;
    }

    memset(&addr, 0, sizeof(addr) );
    addr.sin_family = AF_INET;


    memcpy((char *)&addr.sin_addr, (char *)hostent->h_addr, hostent->h_length);
    addr.sin_port = htons(port);

    if ( connect(pT->sock,(struct sockaddr *) &addr, sizeof(addr))<0)
    {
      rc=WSAGetLastError();
      PRINTL((szTxt,"connect() %s@%d not running.",pT->szServer,port));
      PRINTL((szTxt,"connect() failed errno=%d ",rc));
      closesocket(pT->sock);
      pT->sock = 0;
      WSACleanup();
      return rc;

    }
  }

  /* Next phase is the socket select */
  {
    fd_set              ready = { 0};
    struct timeval      timeout;
    int nSelect;
    FD_ZERO ( &ready);
    FD_SET (pT->sock, &ready);
    timeout.tv_sec =  60;
    timeout.tv_usec = 01;

    nSelect = select(pT->sock + 1,
                     &ready, NULL, NULL, &timeout);
    if (nSelect < 0)
    {
      int error = WSAGetLastError();
      PRINTL((szTxt, "Select Failed!, rc=%d", error));
    } 
    else 
    {
      PRINTL((szTxt, "Select OK"));
    }
  }
  index=0;
  p=service;


  /* Connnected OK, Now issue the commands */
  while ((n=fdread(pT->sock,p,chunk-index))>0)
  {  /*read output from socket*/
    p[n] = 0;
    write(fileno(stdout),p,n);                   /*write to out STDOUT*/
    if (strstr(p, "+OK"))
    {
      state++;
      switch (state)
      {
        case 1:
          PRINTL((szTxt,"<<USER"));
          fdwrite(pT->sock,"USER ",5);
          fdwrite(pT->sock,pT->szUser,strlen(pT->szUser));
          fdwrite(pT->sock,"\r\n",2);
          break;
        case 2:
          PRINTL((szTxt,"<<PASS"));
          fdwrite(pT->sock,"PASS ",5);
          fdwrite(pT->sock,pT->szPass,strlen(pT->szPass));
          fdwrite(pT->sock,"\r\n",2);
          break;
        case 3:
          PRINTL((szTxt,"<<STAT"));
          fdwrite(pT->sock,"STAT",4);
          fdwrite(pT->sock,"\r\n",2);
          break;
        case 4:
          PRINTL((szTxt, "<<QUIT"));
          fdwrite(pT->sock,"QUIT",4);
          fdwrite(pT->sock,"\r\n",2);
          break;
      }
    }
    if (strstr(p, "-ERR"))
    {
      PRINTL((szTxt,"<<QUIT"));
      fdwrite(pT->sock,"QUIT",4);
      fdwrite(pT->sock,"\r\n",2);
      closesocket(pT->sock);
      pT->sock = 0;
      WSACleanup();
      return(1);
    }
    switch (state)
    {
      case 1:
        break;
      case 2:
        break;
      case 3:
        break;
      case 4:
        sscanf(p, "%s %d %d", &data, pcntmsg, &msgsiz);
        _strtime( data );
        PRINTL((szTxt, "%s - This mailbox has %d messages waiting, %d size", data, *pcntmsg, msgsiz));
        break;
    }
    index+=n;
    p+=n;
    if (index==chunk)
    {
      chunk+=INITBUF;
      if ( (p=malloc(chunk+1))==(char *)0)
      {
        rc=GetLastError();
        closesocket(pT->sock);
        pT->sock = 0;
        WSACleanup();
        free(service);
        return rc;
      }
      memcpy(p,service,index);
      free(service);
      service=p;
      p+=index;
    }
  }
  *p=0;
  closesocket(pT->sock);
  pT->sock = 0;
  WSACleanup();
  free(service);
  return 0;
}



#ifdef MAINTEST
/***************************************************************************
** Usage:
**  popcheck server userid passw [/p:socksserver]
**
****************************************************************************/
main(int argc, char **argv)
{
  char *p;
  char *socksserver=NULL;
  char *server = NULL;
  char *userid = NULL;
  char *pass = NULL;
  int i;
  int parmer = 0;

  /* check the parms */
  if (argc > 1)
  {
    for (i=1;i<argc ;i++ )
    {
      if ('/' == argv[i][0])
      {
        if ('S' == toupper(argv[i][1]))
        {
          socksserver = &(argv[i][2]);
        }
      } 
      else 
      {
        switch (parmer)
        {
          
          case 0:
            server = argv[i];
            break;
          case 1:
            userid = argv[i];
            break;
          case 2:
            pass = argv[i];
            break;
        }
        parmer++;
      }
    } /* endfor */
    if (NULL == pass )
    {
      printf("Usage:\n");
      printf("  popcheck server userid passwd [/p:socksserver]\n");
    }
    else
    {
      getPop(server, userid, pass, socksserver, 4 ,&p);
    }
  } 
  return(0);
}
#endif


/*********************************************************************
**
** Name: remss
**
** Abstract:
**  This function removes trailing blanks from a string.
**
** Description:
**  Move to end of string (strlen). Move backwards through the string
**  looking for a non-blank character. Store a NULL after this
**  character.
**
** Parameters:
**  eds - the input/output string
**
** Input:
**  see parameters
**
** Output:
**  see parameters
**
** Return Codes:
**  Input string
**
*********************************************************************/

CHAR *remss( CHAR *eds)
{
 register int ba;

 for (ba = strlen(eds); ((ba >= 0) && (eds[ba] <= ' ')); ba--)
      eds[ba] = '\0';
 return(eds);
}


/*********************************************************************
**
** Name: mailThread
**
** Abstract:
**  A thread to check mail
**
** Description:
**  Set status windows, check mail
**
** Parameters:
**  pT
**
** Input:
**  see parameters
**
** Output:
**  see parameters
**
** Return Codes:
**  None.
**
**-----------------------------------------------------------------------
** PSEUDOCODE:
** BEGIN
**  |
** END
*********************************************************************/

void mailThread(PTDATA pT)
{
  DWORD uTimer;
  char szTxt[256];
  int rc;
  while(TRUE)
  {
    pT->alive = 0;
    SendMessage(pT->hwndStatus,
                SB_SETTEXT,
                (WPARAM)0,
                (LPARAM)"Checking for new messages...");
    TaskBarChgText(pT->hwndMain, IDW_MAIN, szTxt);
    rc = getPop(pT, &(pT->mailCnt));
    if ((pT->mailCnt < pT->mailChecked) && pT->bLreset)
    {
      pT->mailChecked = 0;
    }
    Sleep(500);
    SendMessage(pT->hwndStatus,
                SB_SETTEXT,
                (WPARAM)0,
                (LPARAM)"");
    if (rc)
    {
      sprintf(szTxt, "Mail server connect failed, return code %d", rc);
    }
    else
    {
      sprintf(szTxt, "%d unread messages waiting for %s", 
             (pT->mailCnt - pT->mailChecked), 
             pT->szUser);

    }
    SendMessage(pT->hwndStatus,
                SB_SETTEXT,
                (WPARAM)1,
                (LPARAM)szTxt);
    TaskBarChgText(pT->hwndMain, IDW_MAIN, szTxt);
    for (uTimer = 0; uTimer < pT->uDelay; uTimer++)
    {
      Sleep(1000);
      if (pT->checkNow)
      {
        pT->checkNow = FALSE;
        break;
      }
    }
  }

}


/*********************************************************************
**
** Name: iconThread
**
** Abstract:
**  A thread to blink the icon
**
** Description:
**  if the mailcount is positive blink the icon
**
** Parameters:
**  pT
**
** Input:
**  see parameters
**
** Output:
**  see parameters
**
** Return Codes:
**  None.
**
**-----------------------------------------------------------------------
** PSEUDOCODE:
** BEGIN
**  |
** END
*********************************************************************/

void iconThread(PTDATA pT)
{
  HICON iMain  = LoadIcon(hInst, MAKEINTRESOURCE(IDW_MAIN));
  HICON iMain2 = LoadIcon(hInst, MAKEINTRESOURCE(IDW_MAIN2));
  int lastcnt = 0;
  int thiscnt = 0;
  while(TRUE)
  {
    Sleep(500);
    thiscnt = pT->mailCnt;
    if (lastcnt != thiscnt)
    {
      if ((thiscnt) && (strlen(pT->szSound)))
        PlaySound(pT->szSound, NULL, SND_ASYNC);
    }
    if(thiscnt > pT->mailChecked)
    {
      SendMessage(pT->hwndMain,
                  WM_SETICON,
                  (WPARAM)ICON_BIG,
                  (LPARAM)iMain2);
      SendMessage(pT->hwndMain,
                  WM_SETICON,
                  (WPARAM)ICON_SMALL,
                  (LPARAM)iMain2);
      TaskBarChgIcon(pT->hwndMain, IDW_MAIN, iMain2);
      Sleep(500);
      SendMessage(pT->hwndMain,
                  WM_SETICON,
                  (WPARAM)ICON_SMALL,
                  (LPARAM)iMain);
      SendMessage(pT->hwndMain,
                  WM_SETICON,
                  (WPARAM)ICON_BIG,
                  (LPARAM)iMain);
      TaskBarChgIcon(pT->hwndMain, IDW_MAIN, iMain);
    }
    lastcnt = thiscnt;
  }

}


/*********************************************************************
**
** Name: monThread
**
** Abstract:
**  A thread to watch for mail thread activity
**
** Description:
**  if the mail thread seems dead take action
**
** Parameters:
**  pT
**
** Input:
**  see parameters
**
** Output:
**  see parameters
**
** Return Codes:
**  None.
**
**-----------------------------------------------------------------------
** PSEUDOCODE:
** BEGIN
**  |
** END
*********************************************************************/

void monThread(PTDATA pT)
{
  DWORD uTimer;
  while(TRUE)
  {
    pT->alive++;
    for (uTimer = 0; (uTimer/2) < pT->uDelay; uTimer++)
    {
      Sleep(1000);
    }
    if ((0 < pT->alive) && (pT->sock))
    {
      shutdown(pT->sock, 2);
    }
  }
}


/*****************************************************************************
**
** Name: DrawBitmap
**
** Abstract:
**  Draw a bitmap in a rectangle
**
** Description:
**  Load the bitmap. Get its size, Blt it. Cleanup.
**
** Parameters:
**  hdc - for drawing
**  id - of bitmap to load
**  rect - destination coordinates
**  rop - raster op parameter for BitBlt
**
** Output:
**  to hdc
**
** Return Codes:
**  none
**
*****************************************************************************/

void DrawBitmap (HDC hdc, WORD id, RECT rect, DWORD rop)
{
  BITMAP bm;            /* Bitmap header                          */
  HDC   hMemDc;
  HANDLE hOldObject;
  HBITMAP hbmp;                   /* For Bitmap loading           */

  /****************************************************************/
  /* extract the bitmap information                               */
  /****************************************************************/

  hbmp = LoadBitmap(hInst,
                    MAKEINTRESOURCE(id));
  GetObject(hbmp, sizeof(BITMAP), (LPSTR)&bm);

  /****************************************************************/
  /* Create a memory DC and select the bitmap into it             */
  /****************************************************************/

  hMemDc = CreateCompatibleDC(hdc);
  hOldObject = SelectObject(hMemDc, hbmp);

  /****************************************************************/
  /* Draw the Bitmap                                              */
  /****************************************************************/

  SetStretchBltMode(hdc, COLORONCOLOR);
  StretchBlt(hdc, 0, 0, rect.right-rect.left, rect.bottom-rect.top,
     hMemDc, 0, 0, bm.bmWidth, bm.bmHeight, rop);

  /****************************************************************/
  /* cleanup...                                                   */
  /****************************************************************/

  SelectObject(hMemDc, hOldObject);
  DeleteObject(hbmp);
  DeleteDC(hMemDc);
}


// TaskBarAddIcon - adds an icon to the taskbar status area. 
// Returns TRUE if successful, or FALSE otherwise. 
// hwnd - handle to the window to receive callback messages. 
// uID - identifier of the icon. 
// hicon - handle to the icon to add. 
// lpszTip - ToolTip text. 

BOOL TaskBarAddIcon(HWND hwnd, UINT uID, HICON hicon, LPSTR lpszTip) 
{ 
    BOOL res; 
    NOTIFYICONDATA tnid; 
 
    tnid.cbSize = sizeof(NOTIFYICONDATA); 
    tnid.hWnd = hwnd; 
    tnid.uID = uID; 
    tnid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP; 
    tnid.uCallbackMessage = UM_MESSAGE; 
    tnid.hIcon = hicon; 
    if (lpszTip) 
      strcpy(tnid.szTip, lpszTip);
    else 
      tnid.szTip[0] = (TCHAR)'\0'; 
 
    res = Shell_NotifyIcon(NIM_ADD, &tnid); 
    return res; 
}


// TaskBarChgIcon - changes the icon in the taskbar status area. 
// Returns TRUE if successful, or FALSE otherwise. 
// hwnd - handle to the window to receive callback messages. 
// uID - identifier of the icon. 
// hicon - handle to the icon to add. 
// lpszTip - ToolTip text. 

BOOL TaskBarChgIcon(HWND hwnd, UINT uID, HICON hicon) 
{ 
    BOOL res; 
    NOTIFYICONDATA tnid; 
 
    tnid.cbSize = sizeof(NOTIFYICONDATA); 
    tnid.hWnd = hwnd; 
    tnid.uID = uID; 
    tnid.uFlags = NIF_ICON; 
    tnid.hIcon = hicon; 
    res = Shell_NotifyIcon(NIM_MODIFY, &tnid); 
    return res; 
}


// TaskBarChgText - changes the text in the taskbar status area. 
// Returns TRUE if successful, or FALSE otherwise. 
// hwnd - handle to the window to receive callback messages. 
// uID - identifier of the icon. 
// hicon - handle to the icon to add. 
// lpszTip - ToolTip text. 

BOOL TaskBarChgText(HWND hwnd, UINT uID, LPSTR lpszTip) 
{ 
    BOOL res; 
    NOTIFYICONDATA tnid; 
 
    tnid.cbSize = sizeof(NOTIFYICONDATA); 
    tnid.hWnd = hwnd; 
    tnid.uID = uID; 
    tnid.uFlags = NIF_TIP; 
    if (lpszTip) 
      strcpy(tnid.szTip, lpszTip);
    else 
      tnid.szTip[0] = (TCHAR)'\0'; 
    res = Shell_NotifyIcon(NIM_MODIFY, &tnid); 
    return res; 
}


// TaskBarDelIcon - adds an icon to the taskbar status area. 
// Returns TRUE if successful, or FALSE otherwise. 
// hwnd - handle to the window to receive callback messages. 
// uID - identifier of the icon. 
// hicon - handle to the icon to add. 
// lpszTip - ToolTip text. 

BOOL TaskBarDelIcon(HWND hwnd, UINT uID) 
{ 
    BOOL res; 
    NOTIFYICONDATA tnid; 
 
    tnid.cbSize = sizeof(NOTIFYICONDATA); 
    tnid.hWnd = hwnd; 
    tnid.uID = uID; 
    res = Shell_NotifyIcon(NIM_DELETE, &tnid); 
    return res; 
}
