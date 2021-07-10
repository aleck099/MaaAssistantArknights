#include "WinMacro.h"

#include <vector>
#include <utility>
#include <ctime>
#include <cassert>
#include <algorithm>

#include <stdint.h>
#include <WinUser.h>

#include <iostream>

#include "Configer.h"

using namespace MeoAssistance;

WinMacro::WinMacro(HandleType type)
    : m_handle_type(type),
    m_rand_engine(time(NULL))
{
}

bool WinMacro::findHandle()
{
    json::array handle_arr;
    switch (m_handle_type) {
    case HandleType::BlueStacksControl:
        handle_arr = Configer::handleObj["BlueStacksControl"].as_array();
        break;
    case HandleType::BlueStacksView:
        handle_arr = Configer::handleObj["BlueStacksView"].as_array();
        break;
    case HandleType::BlueStacksWindow:
        handle_arr = Configer::handleObj["BlueStacksWindow"].as_array();
        break;
    default:
        std::cerr << "handle type error! " << static_cast<int>(m_handle_type) << std::endl;
        return false;
    }

    m_handle = NULL;
    for (auto&& obj : handle_arr)
    {
        m_handle = ::FindWindowExA(m_handle, NULL, obj["class"].as_string().c_str(), obj["window"].as_string().c_str());
    }

#ifdef _DEBUG
    std::cout << "type: " << static_cast<int>(m_handle_type) << ", handle: " << m_handle << std::endl;
#endif

    if (m_handle != NULL) {
        return true;
    }
    else {
        return false;
    }
}

bool WinMacro::resizeWindow(int width, int height)
{
    if (!(static_cast<int>(m_handle_type) & static_cast<int>(HandleType::Window))) {
        return false;
    }

    return ::MoveWindow(m_handle, 100, 100, width, height, true);
}

double WinMacro::getScreenScale()
{
    // 获取窗口当前显示的监视器
    // 使用桌面的句柄.
    HWND hWnd = GetDesktopWindow();
    HMONITOR hMonitor = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);

    // 获取监视器逻辑宽度与高度
    MONITORINFOEX miex;
    miex.cbSize = sizeof(miex);
    GetMonitorInfo(hMonitor, &miex);
    int cxLogical = (miex.rcMonitor.right - miex.rcMonitor.left);
    int cyLogical = (miex.rcMonitor.bottom - miex.rcMonitor.top);

    // 获取监视器物理宽度与高度
    DEVMODE dm;
    dm.dmSize = sizeof(dm);
    dm.dmDriverExtra = 0;
    EnumDisplaySettings(miex.szDevice, ENUM_CURRENT_SETTINGS, &dm);
    int cxPhysical = dm.dmPelsWidth;
    int cyPhysical = dm.dmPelsHeight;

    // 考虑状态栏大小，逻辑尺寸会比实际小
    double horzScale = ((double)cxPhysical / (double)cxLogical);
    double vertScale = ((double)cyPhysical / (double)cyLogical);

    // 考虑状态栏大小，选择里面大的那个
    return std::max(horzScale, vertScale);
}

bool WinMacro::click(Point p)
{
    if (!(static_cast<int>(m_handle_type) & static_cast<int>(HandleType::Control))) {
        return false;
    }

#ifdef _DEBUG
    std::cout << "click: " << p.x << ", " << p.y << std::endl;
#endif

	LPARAM lparam = MAKELPARAM(p.x, p.y);

	::SendMessage(m_handle, WM_LBUTTONDOWN, MK_LBUTTON, lparam);
	::SendMessage(m_handle, WM_LBUTTONUP, 0, lparam);

    return true;
}

bool WinMacro::clickRange(Rect rect)
{
    if (!(static_cast<int>(m_handle_type) & static_cast<int>(HandleType::Control))) {
        return false;
    }

    int x = 0, y = 0;
    if (rect.width == 0) {
        x = rect.x;
    }
    else {
        std::poisson_distribution<int> x_rand(rect.width);
        x = x_rand(m_rand_engine) + rect.x;
    }

    if (rect.height == 0) {
        y = rect.y;
    }
    else {
        std::poisson_distribution<int> y_rand(rect.height);
        y = y_rand(m_rand_engine) + rect.y;
    }

    return click({ x, y });
}

Rect WinMacro::getWindowRect()
{
    RECT rect;
    bool ret = ::GetWindowRect(m_handle, &rect);
    if (!ret) {
        return { 0, 0, 0 ,0 };
    }
    double scale = getScreenScale();
    return Rect{ rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top } * scale;
}

cv::Mat WinMacro::getImage(Rect rect)
{
    if (!(static_cast<int>(m_handle_type) & static_cast<int>(HandleType::View))) {
        return cv::Mat();
    }

    HDC pDC;// 源DC
    //判断是不是窗口句柄如果是的话不能使用GetDC来获取DC 不然截图会是黑屏
    if (m_handle == ::GetDesktopWindow())
    {
        pDC = CreateDCA("DISPLAY", NULL, NULL, NULL);
    }
    else
    {
        pDC = ::GetDC(m_handle);//获取屏幕DC(0为全屏，句柄则为窗口)
    }
    int BitPerPixel = ::GetDeviceCaps(pDC, BITSPIXEL);//获得颜色模式
    if (rect.width == 0 && rect.height == 0)//默认宽度和高度为全屏
    {
        rect.width = ::GetDeviceCaps(pDC, HORZRES); //设置图像宽度全屏
        rect.height = ::GetDeviceCaps(pDC, VERTRES); //设置图像高度全屏
    }
    HDC memDC;//内存DC
    memDC = ::CreateCompatibleDC(pDC);
    HBITMAP memBitmap, oldmemBitmap;//建立和屏幕兼容的bitmap
    memBitmap = ::CreateCompatibleBitmap(pDC, rect.width, rect.height);
    oldmemBitmap = (HBITMAP)::SelectObject(memDC, memBitmap);//将memBitmap选入内存DC
    if (m_handle == ::GetDesktopWindow())
    {
        BitBlt(memDC, 0, 0, rect.width, rect.height, pDC, rect.x, rect.y, SRCCOPY);//图像宽度高度和截取位置
    }
    else
    {
        bool bret = ::PrintWindow(m_handle, memDC, PW_CLIENTONLY);
        if (!bret)
        {
            BitBlt(memDC, 0, 0, rect.width, rect.height, pDC, rect.x, rect.y, SRCCOPY);//图像宽度高度和截取位置
        }
    }

    BITMAP bmp;
    GetObject(memBitmap, sizeof(BITMAP), &bmp);
    int nChannels = bmp.bmBitsPixel == 1 ? 1 : bmp.bmBitsPixel / 8;
    cv::Mat dst_mat;
    dst_mat.create(cv::Size(bmp.bmWidth, bmp.bmHeight), CV_MAKETYPE(CV_8U, nChannels));
    GetBitmapBits(memBitmap, bmp.bmHeight * bmp.bmWidth * nChannels, dst_mat.data);

    DeleteObject(memBitmap);
    DeleteDC(memDC);
    ReleaseDC(m_handle, pDC);

#ifdef _DEBUG
    std::string filename = Configer::getCurDir() + "\\test.bmp";
    cv::imwrite(filename, dst_mat);
#endif

    return dst_mat;
}