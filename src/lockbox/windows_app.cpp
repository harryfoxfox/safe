#include <windows.h>

WINAPI
int
WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
        LPSTR lpCmdLine, int nCmdShow) {
  MessageBox(NULL, "Goodbye, cruel world!", "Note", MB_OK);
  return 0;
}
