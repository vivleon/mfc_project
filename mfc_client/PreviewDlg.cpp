// PreviewDlg.cpp : 구현 파일
//

#include "pch.h"
#include "CanClient.h"
#include "afxdialogex.h"
#include "PreviewDlg.h"
#include "resource.h"

// GDI+ Headers
#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")

// GDI+ Token (Static member for proper initialization/shutdown)
// This should ideally be managed by the application class (CCanClientApp)
// For simplicity here, we use a static variable within the dialog.
// Note: This simple approach might cause issues if multiple dialogs are created/destroyed rapidly.
namespace // Anonymous namespace for static variable
{
    ULONG_PTR gdiplusToken = 0;
    bool gdiplusInitialized = false;

    void EnsureGdiplusInitialized() {
        if (!gdiplusInitialized) {
            Gdiplus::GdiplusStartupInput gdiplusStartupInput;
            Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);
            gdiplusInitialized = true;
            // Ideally, GdiplusShutdown should be called when the application exits.
            // A simple atexit handler might work here for basic cases.
            atexit([] { if (gdiplusInitialized) Gdiplus::GdiplusShutdown(gdiplusToken); });
        }
    }
} // end anonymous namespace


IMPLEMENT_DYNAMIC(CPreviewDlg, CDialogEx)

CPreviewDlg::CPreviewDlg(CWnd* pParent /*=nullptr*/)
    : CDialogEx(IDD_PREVIEW_DLG, pParent)
{
    EnsureGdiplusInitialized(); // Ensure GDI+ is started
}

CPreviewDlg::~CPreviewDlg()
{
    // GDI+ shutdown is handled by atexit handler now (simplified approach)
}

void CPreviewDlg::DoDataExchange(CDataExchange* pDX)
{
    CDialogEx::DoDataExchange(pDX);
}

BEGIN_MESSAGE_MAP(CPreviewDlg, CDialogEx)
END_MESSAGE_MAP()

BOOL CPreviewDlg::OnInitDialog()
{
    CDialogEx::OnInitDialog();

    if (!m_bufLeft.empty()) {
        if (BufferToCBitmap(m_bufLeft, m_bmpLeft)) {
            ((CStatic*)GetDlgItem(IDC_IMG_LEFT))->SetBitmap(m_bmpLeft);
        }
        else { AfxMessageBox(_T("왼쪽 이미지를 로드할 수 없습니다.")); }
    }
    if (!m_bufRight.empty()) {
        if (BufferToCBitmap(m_bufRight, m_bmpRight)) {
            ((CStatic*)GetDlgItem(IDC_IMG_RIGHT))->SetBitmap(m_bmpRight);
        }
        else { AfxMessageBox(_T("오른쪽 이미지를 로드할 수 없습니다.")); }
    }

    return TRUE;
}

void CPreviewDlg::SetImageBuffer(const std::vector<unsigned char>& buffer, int index)
{
    if (index == 0) m_bufLeft = buffer;
    else m_bufRight = buffer;
}

bool CPreviewDlg::BufferToCBitmap(const std::vector<unsigned char>& buffer, CBitmap& bitmap)
{
    try {
        cv::Mat mat = cv::imdecode(buffer, cv::IMREAD_COLOR);
        if (mat.empty()) { return false; }
        return MatToCBitmap(mat, bitmap);
    }
    catch (const cv::Exception& ex) { /* Log error */ return false; }
}

bool CPreviewDlg::MatToCBitmap(const cv::Mat& mat, CBitmap& bitmap)
{
    if (mat.empty()) return false;

    int width = mat.cols;
    int height = mat.rows;
    int channels = mat.channels();
    cv::Mat tempMat = mat; // Create a modifiable copy if needed

    // Ensure 3 channels (BGR) for SetDIBitsToDevice
    if (channels == 1) { cv::cvtColor(mat, tempMat, cv::COLOR_GRAY2BGR); }
    else if (channels == 4) { cv::cvtColor(mat, tempMat, cv::COLOR_BGRA2BGR); }
    else if (channels != 3) { return false; } // Unsupported format

    if (bitmap.GetSafeHandle()) { bitmap.DeleteObject(); }

    // [FIX] CreateCompatibleBitmap needs a CDC*
    CClientDC screenDC(NULL); // Get DC for the screen
    if (!bitmap.CreateCompatibleBitmap(&screenDC, width, height)) {
        return false;
    }

    // Prepare BITMAPINFO
    BITMAPINFO bi;
    ZeroMemory(&bi, sizeof(BITMAPINFO));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = width;
    bi.bmiHeader.biHeight = -height; // Top-down DIB for SetDIBitsToDevice
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 24; // 3 channels * 8 bits
    bi.bmiHeader.biCompression = BI_RGB;

    // Use SetDIBits to copy data directly
    // Ensure the CBitmap object is selected into a DC before calling SetDIBits? No, needed for GetDIBits.
    // SetDIBits works directly on the HBITMAP handle.
    // Need a compatible DC to call SetDIBitsToDevice? Check documentation.
    // SetDIBits function might be better here as it works directly with HBITMAP.

    // Let's stick to SetDIBitsToDevice as it's common, requires a DC.
    CDC memDC;
    memDC.CreateCompatibleDC(&screenDC); // Create a memory DC compatible with the screen
    CBitmap* pOldBmp = memDC.SelectObject(&bitmap);

    // SetDIBitsToDevice expects the data pointer (tempMat.data)
    int result = SetDIBitsToDevice(
        memDC.GetSafeHdc(), // Target DC
        0, 0,             // Destination x, y
        width, height,    // Width, Height
        0, 0,             // Source x, y
        0,                // Start scan line
        height,           // Number of scan lines
        tempMat.data,     // Pointer to image data
        &bi,              // Pointer to BITMAPINFO
        DIB_RGB_COLORS    // Color usage
    );

    memDC.SelectObject(pOldBmp); // Restore old bitmap
    memDC.DeleteDC();           // Clean up memory DC

    if (result == 0) {
        // Error occurred
        DWORD error = GetLastError();
        CString msg;
        msg.Format(L"SetDIBitsToDevice failed with error code: %lu", error);
        AfxMessageBox(msg);
        bitmap.DeleteObject(); // Clean up bitmap if failed
        return false;
    }

    return true;
}

