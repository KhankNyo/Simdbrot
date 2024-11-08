
#include <windows.h>
#include <stdio.h>

#include "Common.h"
#define MAINTHREAD_CREATE_WINDOW (WM_USER + 0)
#define MAINTHREAD_DESTROY_WINDOW (WM_USER + 1)

#define MODE_MAX 9
#define MAX_THREAD_COUNT 128


/* Casey case because it's funny */
typedef enum win32_menu_item 
{
    MAINMENU_ABOUT = 2,
    MAINMENU_RESET,
    MAINMENU_CHANGE_MODE,
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

typedef struct win32_render_thread_context 
{
    double MaxValue;
    int IterationCount, RenderMode;

    coordmap Map;
    color_buffer ColorBuffer;
} win32_render_thread_context;

typedef struct win32_main_thread_state 
{

    HWND WindowManager,
         MainWindow;
    coordmap Map;
    int IterationCount;

    Bool8 MouseIsDragging;
    int MouseX, MouseY;
    int Mode, ThreadCount;
    int FixedBufferWidth, FixedBufferHeight;

    Bool8 KeyWasDown[0x100];
    Bool8 KeyIsDown[0x100];
    double KeyDownInit[0x100];
    unsigned char LastKeyDown;
} win32_main_thread_state;

typedef struct win32_window_dimension 
{
    int x, y, w, h;
} win32_window_dimension;

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

static win32_window_dimension Win32_GetWindowDimension(HWND Window)
{
    RECT Rect;
    GetClientRect(Window, &Rect);
    return (win32_window_dimension) {
        .x = Rect.left,
        .y = Rect.top,
        .w = Rect.right - Rect.left,
        .h = Rect.bottom - Rect.top,
    };
}

static win32_paint_context Win32_BeginPaint(HWND Window)
{
    HDC FrontDC = GetDC(Window);
    if (NULL == FrontDC)
    {
        Win32_Fatal("unable to retrieve device context of the window.");
    }
    HDC BackDC = CreateCompatibleDC(FrontDC);
    if (NULL == BackDC)
    {
        Win32_Fatal("unable to retrieve device context of the window.");
    }

    win32_window_dimension Dimension = Win32_GetWindowDimension(Window);
    int Width = Dimension.w,
        Height = Dimension.h;

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
    case WM_LBUTTONUP:
    case WM_LBUTTONDOWN:
    case WM_COMMAND:
    {
        PostThreadMessageA(Win32_MainThreadID, Msg, WParam, LParam);
    } break;

    default: Result = DefWindowProcA(Window, Msg, WParam, LParam);
    }

    return Result;
}


static void ZoomMap(win32_main_thread_state *State, int Zoom)
{
    double Scale = Zoom < 0 
        ? 1.1
        : 0.9;

    win32_window_dimension Dimension = Win32_GetWindowDimension(State->MainWindow);
    double MouseX = (double)State->MouseX / Dimension.w * State->Map.Width - State->Map.Left;
    double MouseY = State->Map.Top - (double)State->MouseY / Dimension.h * State->Map.Height;

    double ScaledLeft = (State->Map.Left + MouseX) * Scale - MouseX;
    double ScaledTop = (State->Map.Top - MouseY) * Scale + MouseY;
    double ScaledWidth = State->Map.Width * Scale;
    double ScaledHeight = State->Map.Height * Scale;

    State->Map.Left = ScaledLeft;
    State->Map.Top = ScaledTop;
    State->Map.Width = ScaledWidth;
    State->Map.Height = ScaledHeight;
}

static void ResetMap(win32_main_thread_state *State)
{
    State->Map = (coordmap) {
        .Left = 2,
        .Width = 3,
        .Top = 1,
        .Height = 2,
        .Delta = 0,
    };
    State->IterationCount = 400;
}

static void ChangeMode(win32_main_thread_state *State)
{
    State->Mode++;
    if (State->Mode > MODE_MAX)
        State->Mode = 0;
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
            int x = (i16)LOWORD(Message.lParam);
            int y = (i16)HIWORD(Message.lParam);
            if (State->MouseIsDragging)
            {
                win32_window_dimension Dimension = Win32_GetWindowDimension(State->MainWindow);
                double Dx = x - State->MouseX;
                double Dy = y - State->MouseY;
                double WorldDx = Dx / Dimension.w * State->Map.Width;
                double WorldDy = Dy / Dimension.h * State->Map.Height;
                State->Map.Left += WorldDx;
                State->Map.Top += WorldDy;
            }

            State->MouseX = x;
            State->MouseY = y;
        } break;
        case WM_LBUTTONUP:
        case WM_LBUTTONDOWN:
        {
            State->MouseIsDragging = Message.message == WM_LBUTTONDOWN;
        } break;
        case WM_MOUSEWHEEL:
        {
            ZoomMap(State, GET_WHEEL_DELTA_WPARAM(Message.wParam));
        } break;

        case WM_COMMAND:
        {
            /* menu messages */
            if (0 == HIWORD(Message.wParam))
            {
                switch ((win32_menu_item)LOWORD(Message.wParam))
                {
                case MAINMENU_ABOUT:
                {
                } break;
                case MAINMENU_RESET:
                {
                    ResetMap(State);
                } break;
                case MAINMENU_CHANGE_MODE:
                {
                    ChangeMode(State);
                } break;
                }
            }
        } break;
        case WM_KEYUP:
        case WM_KEYDOWN:
        {
            Bool8 KeyIsDown = Message.message == WM_KEYDOWN;
            unsigned char Key = Message.wParam & 0xFF;
            State->LastKeyDown = Key;
            State->KeyIsDown[Key] = KeyIsDown;

            if (KeyIsDown && State->KeyDownInit[Key] == 0)
            {
                State->KeyDownInit[Key] = Win32_GetTimeMillisec();
            }
            else if (!KeyIsDown)
            {
                State->KeyDownInit[Key] = 0;
            }
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

static Bool8 Win32_IsKeyDown(win32_main_thread_state *State, char Key, double Delay)
{
    unsigned char k = Key;
    double Time = Win32_GetTimeMillisec();
    Bool8 IsDown = State->KeyIsDown[k] && Time - State->KeyDownInit[k] > Delay;
    if (IsDown)
        State->KeyDownInit[k] = Time;
    return IsDown;
}

static const char *GetSimdMode(int Mode)
{
    switch (Mode)
    {
    default:
    case 0: return "regular f32x1";
    case 1: return "regular f64x1";
    case 2: return "sse f32x4";
    case 3: return "sse f64x2";
    case 4: return "sse f32x4 (with fma)";
    case 5: return "sse f64x2 (with fma)";
    case 6: return "avx f32x8";
    case 7: return "avx f64x4";
    case 8: return "avx f32x8 (with fma)";
    case 9: return "avx f64x4 (with fma)";
    }
}

static DWORD Win32_RenderThread(LPVOID UserData)
{
    win32_render_thread_context *ThreadContext = UserData;
    typedef void (*MandelbrotRenderFn)(
        color_buffer *ColorBuffer,
        const coordmap *Map,
        int IterationCount, 
        double MaxValue
    );
    static const MandelbrotRenderFn Render[MODE_MAX + 1] = {
        RenderMandelbrotSet32_Unopt,
        RenderMandelbrotSet64_Unopt,
        RenderMandelbrotSet32_SSE,
        RenderMandelbrotSet64_SSE,
        RenderMandelbrotSet32_SSEFMA,
        RenderMandelbrotSet64_SSEFMA,
        RenderMandelbrotSet32_AVX,
        RenderMandelbrotSet64_AVX,
        RenderMandelbrotSet32_AVXFMA,
        RenderMandelbrotSet64_AVXFMA
    };

    Render[ThreadContext->RenderMode](
        &ThreadContext->ColorBuffer, 
        &ThreadContext->Map, 
        ThreadContext->IterationCount, 
        ThreadContext->MaxValue
    );

    return 0;
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
    AppendMenuA(MainMenu, MF_STRING, MAINMENU_RESET, "&Reset");
    AppendMenuA(MainMenu, MF_STRING, MAINMENU_CHANGE_MODE, "&Change rendering mode");
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
        .ThreadCount = 4,
        .Mode = 0,
        .FixedBufferWidth = 240,
        .FixedBufferHeight = 180
    };
    ResetMap(&State);

    {
        POINT Point;
        GetCursorPos(&Point);
        RECT Rect;
        GetClientRect(MainWindow, &Rect);
        State.MouseX = Point.x - Rect.left;
        State.MouseY = Point.y - Rect.top;
    }
    color_buffer Buffer;
    GetDefaultPalette(Buffer.Palette);
    double LastTime = Win32_GetTimeMillisec();
    double ElapsedTime = 0;
    double MillisecPerFrame = 1000.0 / 60.0;
    double MaxValue = 4.0;
    double KeyDelay = 50;

    win32_render_thread_context RenderThreadContext[MAX_THREAD_COUNT] = { 0 };
    HANDLE RenderThreadHandles[MAX_THREAD_COUNT] = { 0 };
#define FIXED_BUFFER_MAX_WIDTH 1080
#define FIXED_BUFFER_MAX_HEIGHT 720
#define FIXED_BUFFER_MIN_WIDTH 160
#define FIXED_BUFFER_MIN_HEIGHT 80
#define FIXED_BUFFER_MIN_SIZE (160*80)
    static u32 FixedBuffer[FIXED_BUFFER_MAX_WIDTH * FIXED_BUFFER_MAX_HEIGHT];
    while (Win32_PollInputs(&State))
    {
        if (Win32_IsKeyPressed(&State, 'C'))
            ChangeMode(&State);
        if (Win32_IsKeyPressed(&State, 'R'))
            ResetMap(&State);
        if (Win32_IsKeyDown(&State, 'Z', KeyDelay))
            ZoomMap(&State, 1);
        if (Win32_IsKeyDown(&State, 'X', KeyDelay))
            ZoomMap(&State, -1);
        if (Win32_IsKeyDown(&State, VK_UP, KeyDelay) && State.IterationCount <= 1000000)
            State.IterationCount++;
        if (Win32_IsKeyDown(&State, VK_DOWN, KeyDelay) && State.IterationCount > 1)
            State.IterationCount--;
        if (Win32_IsKeyPressed(&State, VK_LEFT) && State.ThreadCount > 1)
            State.ThreadCount--;
        if (Win32_IsKeyPressed(&State, VK_RIGHT) && State.ThreadCount <= MAX_THREAD_COUNT)
            State.ThreadCount++;

        if (ElapsedTime > MillisecPerFrame)
        {
            win32_window_dimension Dimension = Win32_GetWindowDimension(MainWindow);
            int WindowWidth = Dimension.w;
            win32_paint_context Context;
            HDC DC;
            Bool8 UsingFixedBuffer = State.FixedBufferWidth * State.FixedBufferHeight > FIXED_BUFFER_MIN_SIZE;
            if (UsingFixedBuffer)
            {
                DC = GetDC(MainWindow);
                if (NULL == DC)
                    Win32_Fatal("Unable to retriece the window's device context.");

                State.FixedBufferWidth = MIN(State.FixedBufferWidth, FIXED_BUFFER_MAX_WIDTH);
                State.FixedBufferHeight = MIN(State.FixedBufferHeight, FIXED_BUFFER_MAX_HEIGHT);
                State.FixedBufferWidth = MAX(State.FixedBufferWidth, FIXED_BUFFER_MIN_WIDTH);
                State.FixedBufferHeight = MAX(State.FixedBufferHeight, FIXED_BUFFER_MIN_HEIGHT);

                Buffer.Ptr = FixedBuffer;
                Buffer.Width = State.FixedBufferWidth;
                Buffer.Height = State.FixedBufferHeight;
                State.Map.Delta = State.Map.Height / Buffer.Height;
            }
            else
            {
                Context = Win32_BeginPaint(MainWindow);
                DC = Context.Back;
                Buffer.Ptr = Context.BitmapData;
                Buffer.Width = Context.Width;
                Buffer.Height = Context.Height;
                State.Map.Delta = State.Map.Height / Buffer.Height;
            }

                int BufferHeightForSingleThread = Buffer.Height / State.ThreadCount;
                int RemainingHeight = Buffer.Height % State.ThreadCount;
                double MapHeightForSingleThread = (double)BufferHeightForSingleThread / Buffer.Height * State.Map.Height;
                double RemainingMapHeight = (double)RemainingHeight / Buffer.Height * State.Map.Height;
                for (int i = 0; i < State.ThreadCount; i++)
                {
                    RenderThreadContext[i] = (win32_render_thread_context) {
                        .RenderMode = State.Mode,
                        .MaxValue = MaxValue,
                        .IterationCount = State.IterationCount,
                        .Map = (coordmap) {
                            .Delta = State.Map.Delta,
                            .Left = State.Map.Left,
                            .Width = State.Map.Width,

                            .Top = State.Map.Top - i*MapHeightForSingleThread,
                            .Height = MapHeightForSingleThread,
                        },
                        .ColorBuffer = (color_buffer) {
                            .Ptr = Buffer.Ptr + i * Buffer.Width * BufferHeightForSingleThread,
                            .Width = Buffer.Width,
                            .Height = BufferHeightForSingleThread,
                        },
                    };
                    if (i == State.ThreadCount - 1)
                    {
                        RenderThreadContext[i].ColorBuffer.Height += RemainingHeight;
                        RenderThreadContext[i].Map.Top -= RemainingMapHeight;
                        RenderThreadContext[i].Map.Height += RemainingMapHeight;
                    }
                    memcpy(RenderThreadContext[i].ColorBuffer.Palette, Buffer.Palette, sizeof Buffer.Palette);

                    DWORD ID;
                    RenderThreadHandles[i] = CreateThread(
                        NULL, 
                        1024, 
                        Win32_RenderThread, 
                        &RenderThreadContext[i], 
                        0, 
                        &ID
                    );
                    /* TODO: err checking */
                }
                for (int i = 0; i < State.ThreadCount; i++)
                {
                    WaitForSingleObject(RenderThreadHandles[i], INFINITE);
                    CloseHandle(RenderThreadHandles[i]);
                }

                if (UsingFixedBuffer)
                {
                    static BITMAPINFO FixedBufferInfo = {
                        .bmiHeader = {
                            .biSize = sizeof FixedBufferInfo, 
                            .biPlanes = 1,
                            .biBitCount = 32, 
                            .biCompression = BI_RGB, 
                        },
                    };
                    FixedBufferInfo.bmiHeader.biWidth = Buffer.Width,
                    FixedBufferInfo.bmiHeader.biHeight = -Buffer.Height, 
                    StretchDIBits(DC, 
                        0, 0, Dimension.w, Dimension.h, 
                        0, 0, Buffer.Width, Buffer.Height, 
                        FixedBuffer, &FixedBufferInfo, 
                        DIB_RGB_COLORS, SRCCOPY
                    );
                }
                


                TEXTMETRICA TextStat;
                GetTextMetricsA(DC, &TextStat);

                char TmpTxt[512];
                int LineCount = 6;
                int Len = snprintf(TmpTxt, sizeof TmpTxt, 
                    "FPS: %3.2f\n"
                    "x: %3.5f .. %3.5f\n"
                    "y: %3.5f .. %3.5f\n"
                    "iteration%s: %d\n"
                    "thread%s: %d\n"
                    "rendering: %s", 
                    (double)1000.0 / ElapsedTime,
                    (double)-State.Map.Left, 
                    (double)-State.Map.Left + State.Map.Width,
                    (double)State.Map.Top - State.Map.Height, 
                    (double)State.Map.Top, 
                    State.IterationCount != 1? "s":"", State.IterationCount,
                    State.ThreadCount != 1? "s":"", State.ThreadCount,
                    GetSimdMode(State.Mode)
                );

                RECT TopRight = {
                    .left = WindowWidth - 30 * TextStat.tmMaxCharWidth,
                    .right = WindowWidth,
                    .top = 0,
                    .bottom = TextStat.tmHeight * LineCount,
                };
                SetTextColor(DC, 0x0000FF00);
                SetBkColor(DC, 0);
                DrawTextA(DC, TmpTxt, Len, &TopRight, DT_RIGHT);
            if (UsingFixedBuffer)
                ReleaseDC(MainWindow, DC);
            else Win32_EndPaint(MainWindow, &Context);


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

