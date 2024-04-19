
#include <windows.h>
#include <stdio.h>

#include "Common.h"
#define MAINTHREAD_CREATE_WINDOW (WM_USER + 0)
#define MAINTHREAD_DESTROY_WINDOW (WM_USER + 1)



/* Casey case because it's funny */
typedef enum win32_menu_item 
{
    MAINMENU_ABOUT = 2,
} win32_menu_item;

typedef struct win32_window_creation_args 
{
    DWORD ExStyle;
    DWORD Style;
    const char *ClassName;
    const char *WindowName;
    int x, y, w, h;
    HWND ParentWindow;
    HMENU Menu;
} win32_window_creation_args;

typedef struct win32_main_thread_state 
{
    Bool8 KeyWasDown[0x100];
    Bool8 KeyIsDown[0x100];
    unsigned char LastKeyDown;

    HWND WindowManager,
         MainWindow;
    coordmap64 Map64;
    double MouseTopPercentage;
    double MouseLeftPercentage;
} win32_main_thread_state;

typedef struct win32_paint_context 
{
    HDC Back, Front;
    HBITMAP BitmapHandle;
    int Width, Height;
    void *BitmapData;
} win32_paint_context;



static DWORD Win32_MainThreadID;
static double Win32_PerfCounterResolutionMillisec;

static void Win32_Fatal(const char *ErrMsg)
{
    MessageBoxA(NULL, ErrMsg, "Fatal Error", MB_ICONERROR);
    ExitProcess(1);
}

static double Win32_GetTimeMillisec(void)
{
    LARGE_INTEGER Li;
    QueryPerformanceCounter(&Li);
    return Win32_PerfCounterResolutionMillisec * (double)Li.QuadPart;
}

static win32_paint_context Win32_BeginPaint(HWND Window)
{
    HDC FrontDC = GetDC(Window);
    HDC BackDC = CreateCompatibleDC(FrontDC);

    RECT ClientRect;
    GetClientRect(Window, &ClientRect);
    int Width = ClientRect.right - ClientRect.left,
        Height = ClientRect.bottom - ClientRect.top;

    BITMAPINFO BitmapInfo = {
        .bmiHeader = {
            .biSize = sizeof BitmapInfo,
            .biWidth = Width,
            .biHeight = -Height,
            .biPlanes = 1,
            .biBitCount = 32,
            .biCompression = BI_RGB,
        },
    };
    void *Ptr;
    HBITMAP BitmapHandle = CreateDIBSection(BackDC, &BitmapInfo, DIB_RGB_COLORS, &Ptr, NULL, 0);
    if (NULL == BitmapHandle)
    {
        Win32_Fatal("Unable to create a bitmap.");
    }

    SelectObject(BackDC, BitmapHandle);
    win32_paint_context Context = {
        .Back = BackDC,
        .Front = FrontDC,
        .BitmapHandle = BitmapHandle,

        .Width = Width,
        .Height = Height,
        .BitmapData = Ptr,
    };
    return Context;
}

static void Win32_EndPaint(HWND Window, win32_paint_context *Context)
{
    BitBlt(Context->Front, 0, 0, Context->Width, Context->Height, Context->Back, 0, 0, SRCCOPY);
    DeleteObject(Context->Back);
    DeleteObject(Context->BitmapHandle);
    ReleaseDC(Window, Context->Front);
}



static LRESULT CALLBACK Win32_ProcessMainThreadMessage(HWND Window, UINT Msg, WPARAM WParam, LPARAM LParam)
{
    /* using Casey's DTC */
    LRESULT Result = 0;
    switch (Msg)
    {
    case MAINTHREAD_CREATE_WINDOW:
    {
        win32_window_creation_args *Args = (win32_window_creation_args *)WParam;
        Result = (LRESULT)CreateWindowExA(
            Args->ExStyle, 
            Args->ClassName,
            Args->WindowName, 
            Args->Style, 
            Args->x, 
            Args->y, 
            Args->w, 
            Args->h, 
            Args->ParentWindow, 
            Args->Menu, 
            NULL, 
            NULL
        );
    } break;
    case MAINTHREAD_DESTROY_WINDOW:
    {
        HWND WindowHandle = (HWND)WParam;
        CloseWindow(WindowHandle);
    } break;
    default: Result = DefWindowProcA(Window, Msg, WParam, LParam);
    }
    return Result;
}

static LRESULT CALLBACK Win32_MainWndProc(HWND Window, UINT Msg, WPARAM WParam, LPARAM LParam)
{
    LRESULT Result = 0;
    switch (Msg)
    {
    case WM_CLOSE:
    case WM_QUIT:
    {
        /* (from msg thread) send a close msg to the main thread, 
         * it'll then send back a close window message and exit the entire process */
        PostThreadMessageA(Win32_MainThreadID, WM_CLOSE, (WPARAM)Window, 0);
    } break;
    case WM_MOUSEWHEEL:
    case WM_MOUSEMOVE:
    {
        PostThreadMessageA(Win32_MainThreadID, Msg, WParam, LParam);
    } break;

    default: Result = DefWindowProcA(Window, Msg, WParam, LParam);
    }

    return Result;
}

static Bool8 Win32_PollInputs(win32_main_thread_state *State)
{
    MSG Message;
    State->KeyWasDown[State->LastKeyDown] = State->KeyIsDown[State->LastKeyDown];
    while (PeekMessageA(&Message, 0, 0, 0, PM_REMOVE))
    {
        switch (Message.message)
        {
        case WM_QUIT:
        case WM_CLOSE: 
        {
            HWND Window = (HWND)Message.wParam;
            SendMessageA(State->WindowManager, MAINTHREAD_DESTROY_WINDOW, (WPARAM)Window, 0);
            return false;
        } break;

        case WM_MOUSEMOVE:
        {
            double x = (i16)LOWORD(Message.lParam);
            double y = (i16)LOWORD(Message.lParam);
            RECT Rect;
            GetClientRect(State->MainWindow, &Rect);
            State->MouseLeftPercentage = x / (Rect.right - Rect.left);
            State->MouseTopPercentage = y / (Rect.bottom - Rect.top);
        } break;
        case WM_MOUSEWHEEL:
        {
            double Res = GET_WHEEL_DELTA_WPARAM(Message.wParam) < 0 ? -.1: .1;
            double MouseDeltaX = State->MouseLeftPercentage + State->Map64.Left / State->Map64.Width;
            double MouseDeltaY = (1 - State->MouseTopPercentage) - State->Map64.Top / State->Map64.Height;
            double Delta = State->Map64.Width * Res;
            State->Map64.Width -= Delta;
            State->Map64.Left += Delta * MouseDeltaX;
            State->Map64.Top -= Delta * MouseDeltaY;
            State->Map64.Height -= Delta;
        } break;

        case WM_COMMAND:
        {
            /* menu messages */
            if (HIWORD(Message.wParam))
            {
            }
        } break;
        case WM_KEYUP:
        case WM_KEYDOWN:
        {
            unsigned char Key = Message.wParam & 0xFF;
            State->LastKeyDown = Key;
            State->KeyIsDown[Key] = Message.message == WM_KEYDOWN;
        } break;
        }
    }
    return true;
}

static Bool8 Win32_IsKeyPressed(const win32_main_thread_state *State, char Key)
{
    unsigned char k = Key;
    return !State->KeyIsDown[k] && State->KeyWasDown[k];
}


static DWORD Win32_Main(LPVOID UserData)
{
    HWND WindowManager = UserData;
    WNDCLASSEXA WndCls = {
        .lpfnWndProc = Win32_MainWndProc,
        .lpszClassName = "SimdbrotCls",
        .cbSize = sizeof WndCls,
        .style = CS_VREDRAW | CS_HREDRAW | CS_OWNDC, 

        .hIcon = LoadIconA(NULL, IDI_APPLICATION),
        .hCursor = LoadCursorA(NULL, IDC_ARROW),
        .hInstance = GetModuleHandleA(NULL),
    };
    RegisterClassExA(&WndCls);

    HMENU MainMenu = CreateMenu();
    win32_window_creation_args Args = {
        .x = CW_USEDEFAULT, 
        .y = CW_USEDEFAULT, 
        .w = 1080,
        .h = 720,
        .Menu = MainMenu,
        .Style = WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        .ExStyle = WS_EX_OVERLAPPEDWINDOW,
        .ParentWindow = NULL,
        .ClassName = WndCls.lpszClassName,
        .WindowName = "Simdbrot",
    };
    HWND MainWindow = (HWND)SendMessageA(WindowManager, MAINTHREAD_CREATE_WINDOW, (WPARAM)&Args, 0);
    if (NULL == MainWindow)
    {
        Win32_Fatal("Unable to create window");
    }
    ShowWindow(MainWindow, SW_SHOW);


    win32_main_thread_state State = { 
        .WindowManager = WindowManager,
        .MainWindow = MainWindow,
        .Map64 = {
            .Left = -2, 
            .Top = 1, 
            .Height = 2,
            .Width = 3,
        },
    };
    color_buffer Buffer;
    GetDefaultPalette(Buffer.Palette);
    double LastTime = Win32_GetTimeMillisec();
    double ElapsedTime = 0;
    double MillisecPerFrame = 1000.0 / 60.0;
    int IterationCount = 512;
    double MaxValue = 2.0;
    Bool8 SimdMode = false;
    while (Win32_PollInputs(&State))
    {
        if (Win32_IsKeyPressed(&State, 'T'))
            SimdMode = !SimdMode;

        if (ElapsedTime > MillisecPerFrame)
        {
            win32_paint_context Context = Win32_BeginPaint(MainWindow);
            {
                Buffer.Ptr = Context.BitmapData;
                Buffer.Width = Context.Width;
                Buffer.Height = Context.Height;
                State.Map64.Delta = State.Map64.Height / Buffer.Height;

                if (SimdMode)
                    RenderMandelbrotSet64_AVX256(&Buffer, State.Map64, IterationCount, MaxValue);
                else
                    RenderMandelbrotSet64_Unopt(&Buffer, State.Map64, IterationCount, MaxValue);


                char TmpTxt[64];
                int Len = snprintf(TmpTxt, sizeof TmpTxt, "FPS: %3.2f\n", 1000.0 / ElapsedTime);
                TEXTMETRICA TextStat;
                GetTextMetricsA(Context.Back, &TextStat);
                RECT TopRight = {
                    .left = Context.Width - Len * TextStat.tmMaxCharWidth,
                    .right = Context.Width,
                    .top = 0,
                    .bottom = TextStat.tmHeight,
                };
                SetTextColor(Context.Back, 0x0000FF00);
                SetBkColor(Context.Back, 0x00000000);
                DrawText(Context.Back, TmpTxt, Len, &TopRight, DT_RIGHT);
            }
            Win32_EndPaint(MainWindow, &Context);
            ElapsedTime = 0;
        }
        double CurrentTime = Win32_GetTimeMillisec();
        ElapsedTime += CurrentTime - LastTime;
        LastTime = CurrentTime;
    }

    /* windows deallocate resources faster than us, so we let them handle that */
    (void)MainWindow;
    (void)MainMenu;
    ExitProcess(0);
}

int WinMain(HINSTANCE Instance, HINSTANCE PrevInstance, PCHAR CmdLine, int CmdShow)
{
    (void)Instance, (void)PrevInstance, (void)CmdLine, (void)CmdShow;
    WNDCLASSEXA WndCls = {
        .lpfnWndProc = Win32_ProcessMainThreadMessage,
        .lpszClassName = "MsgHandlerCls",
        .cbSize = sizeof WndCls,

        .hIcon = LoadIconA(NULL, IDI_APPLICATION),
        .hCursor = LoadCursorA(NULL, IDC_ARROW),
        .hInstance = GetModuleHandleA(NULL),
    };
    (void)RegisterClassExA(&WndCls);

    /* this window is not visible, it's here to process the message queue, 
     * and dispatches any messages that the main thread needs */
    HWND WindowManager = CreateWindowExA(0, WndCls.lpszClassName, "Manager", 0, 0, 0, 0, 0, NULL, NULL, Instance, NULL);
    if (NULL == WindowManager)
    {
        Win32_Fatal("Unable to create window (null).");
    }

    {
        LARGE_INTEGER Li;
        QueryPerformanceFrequency(&Li);
        Win32_PerfCounterResolutionMillisec = 1000.0 / Li.QuadPart;
    }

    /* this is the main thread, the one that does actual work */
    CreateThread(NULL, 0, Win32_Main, WindowManager, 0, &Win32_MainThreadID);

    /* process the message queue */
    /* don't need any mechanism to exit the loop, 
     * because once the main thread that we've created exits, the whole program exits */
    while (1)
    {
        MSG Message;
        GetMessage(&Message, 0, 0, 0);
        TranslateMessage(&Message);

        UINT Event = Message.message;
        if (Event == WM_KEYDOWN
        || Event == WM_KEYUP
        || Event == WM_LBUTTONDOWN
        || Event == WM_LBUTTONUP
        || Event == WM_SIZE
        || Event == WM_CLOSE
        || Event == WM_QUIT
        || Event == WM_MOUSEWHEEL
        || Event == WM_MOUSEMOVE)
        {
            PostThreadMessageA(Win32_MainThreadID, Event, Message.wParam, Message.lParam);
        }
        else
        {
            DispatchMessageA(&Message); 
        }
    }
    return 0;
}

