// Copyright 2001-2016 Crytek GmbH / Crytek Group. All rights reserved.

#include "StdAfx.h"
#include <CryString/UnicodeFunctions.h>
#include "DriverD3D.h"
#include <CryCore/Platform/WindowsUtils.h>

#if CRY_PLATFORM_DURANGO

// Uncomment to allow VSGD capturing
// Must be run with legacy driver
// game will run very slowly when enabled because of the API wrapper
//#define DURANGO_VSGD_CAP
	#if defined(DURANGO_VSGD_CAP)
		#include <vsgcapture.h>
	#endif

	#if defined(DURANGO_MONOD3D_DRIVER)
LINK_SYSTEM_LIBRARY("d3d11_x.lib")
	#else
		#if defined(ENABLE_PROFILING_CODE)
			#define USE_INSTRUMENTED_LIBS
		#endif

		#if defined(USE_INSTRUMENTED_LIBS)
LINK_SYSTEM_LIBRARY("d3d11i.lib")
		#else
LINK_SYSTEM_LIBRARY("d3d11.lib")
		#endif
	#endif

#endif

#include "D3DStereo.h"
#include "D3DPostProcess.h"
#include "D3DREBreakableGlassBuffer.h"
#include "NullD3D11Device.h"
#include "PipelineProfiler.h"
#include <CryInput/IHardwareMouse.h>

#define CRY_AMD_AGS_USE_DLL
#include <CryCore/Platform/CryLibrary.h>

#if CRY_PLATFORM_WINDOWS
	#if defined(USE_AMD_API)
		#if !defined(CRY_AMD_AGS_USE_DLL) // Set to 0 to load DLL at runtime, you need to redist amd_ags(64).dll yourself
			#if CRY_PLATFORM_64BIT
LINK_THIRD_PARTY_LIBRARY("SDKs/AMD/AGS Lib/lib/x64/static/amd_ags64.lib")
			#else
LINK_THIRD_PARTY_LIBRARY("SDKs/AMD/AGS Lib/lib/Win32/static/amd_ags.lib")
			#endif
		#else
			#define _AMD_AGS_USE_DLL
		#endif
		#ifndef LoadLibrary
			#define LoadLibrary CryLoadLibrary
			#include <AMD/AGS Lib/inc/amd_ags.h>
			#undef LoadLibrary
		#else
			#include <AMD/AGS Lib/inc/amd_ags.h>
		#endif                              //LoadLibrary
	#endif

	#if defined(USE_NV_API)
		#include <NVAPI/nvapi.h>
		#if CRY_PLATFORM_64BIT
LINK_THIRD_PARTY_LIBRARY("SDKs/NVAPI/amd64/nvapi64.lib")
		#else
LINK_THIRD_PARTY_LIBRARY("SDKs/NVAPI/x86/nvapi.lib")
		#endif
	#endif

	#if defined(USE_AMD_EXT)
		#include <AMD/AMD_Extensions/AmdDxExtDepthBoundsApi.h>

bool g_bDepthBoundsTest = true;
IAmdDxExt* g_pExtension = NULL;
IAmdDxExtDepthBounds* g_pDepthBoundsTest = NULL;
	#endif
#endif

// Only needed to load AMD_AGS
#undef LoadLibrary

#if CRY_PLATFORM_ORBIS
template<class T0, class T1>
HRESULT SetupPresentationParameters(float& fPAR, T0& d3dpp, const T1& videoMode)
{
	d3dpp.BackBufferWidth = CONSOLES_BACKBUFFER_WIDTH;
	d3dpp.BackBufferHeight = CONSOLES_BACKBUFFER_HEIGHT;

	d3dpp.VideoScalerParameters.ScaledOutputWidth = videoMode.dwDisplayWidth;
	d3dpp.VideoScalerParameters.ScaledOutputHeight = videoMode.dwDisplayHeight;

	const float f16_9 = 16.0f / 9.0f;
	const float f4_3 = 4.0f / 3.0f;

	float fOutputAspect = (videoMode.fIsWideScreen) ? f16_9 : f4_3;

	float fBackBufferRatio = (float)d3dpp.BackBufferWidth / (float)d3dpp.BackBufferHeight;

	//float fOutputRatio = (float)videoMode.dwDisplayWidth / (float)videoMode.dwDisplayHeight;
	//float fRatioBackBufferTo16_9 = fBackBufferRatio / f16_9;
	//float fRatioBackBufferToOutputMode  = f16_9 / fOutputRatio;
	//float fRatioOutputModeToDisplay = fOutputRatio / fOutputAspect;
	//fPAR =  fRatioBackBufferTo16_9 * fRatioBackBufferToOutputMode * fRatioOutputModeToDisplay;
	// resolves to...

	fPAR = fBackBufferRatio / fOutputAspect;

	CryLogAlways("	OutDisplay: Width %d, Height %d , Pixel Aspect Ratio %0.3f \n", videoMode.dwDisplayWidth, videoMode.dwDisplayHeight, fPAR);
	CryLogAlways("	OutDisplay: Interlaced %d, WideScreen %d, HiDef %d, Hz %f\n", (uint32)videoMode.fIsInterlaced, (uint32)videoMode.fIsWideScreen, (uint32)videoMode.fIsHiDef, videoMode.RefreshRate);

	return S_OK;
}

#endif

#if CRY_PLATFORM_WINDOWS
// Count monitors helper
static BOOL CALLBACK CountConnectedMonitors(HMONITOR hMonitor, HDC hDC, LPRECT pRect, LPARAM opaque)
{
	uint* count = reinterpret_cast<uint*>(opaque);
	(*count)++;
	return TRUE;
}

#endif

void CD3D9Renderer::DisplaySplash()
{
#if CRY_PLATFORM_WINDOWS
	if (IsEditorMode())
		return;

	HBITMAP hImage = (HBITMAP)LoadImage(GetModuleHandle(0), "splash.bmp", IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE);

	if (hImage != INVALID_HANDLE_VALUE)
	{
		RECT rect;
		HDC hDC = GetDC(m_hWnd);
		HDC hDCBitmap = CreateCompatibleDC(hDC);
		BITMAP bm;

		GetClientRect(m_hWnd, &rect);
		GetObjectA(hImage, sizeof(bm), &bm);
		SelectObject(hDCBitmap, hImage);

		DWORD x = rect.left + (((rect.right - rect.left) - bm.bmWidth) >> 1);
		DWORD y = rect.top + (((rect.bottom - rect.top) - bm.bmHeight) >> 1);

		//    BitBlt(hDC, x, y, bm.bmWidth, bm.bmHeight, hDCBitmap, 0, 0, SRCCOPY);

		RECT Rect;
		GetWindowRect(m_hWnd, &Rect);
		StretchBlt(hDC, 0, 0, Rect.right - Rect.left, Rect.bottom - Rect.top, hDCBitmap, 0, 0, bm.bmWidth, bm.bmHeight, SRCCOPY);

		DeleteObject(hImage);
		DeleteDC(hDCBitmap);
	}
#endif
}

//=====================================================================================

bool CD3D9Renderer::SetCurrentContext(WIN_HWND hWnd)
{
	uint32 i;

	for (i = 0; i < m_RContexts.Num(); i++)
	{
		if (m_RContexts[i]->m_hWnd == hWnd)
			break;
	}
	if (i == m_RContexts.Num())
		return false;

	if (m_CurrContext == m_RContexts[i])
		return true;

	m_CurrContext = m_RContexts[i];

	CHWShader::s_pCurVS = NULL;
	CHWShader::s_pCurPS = NULL;

	return true;
}

bool CD3D9Renderer::CreateContext(WIN_HWND hWnd, bool bMainViewport, int SSX, int SSY)
{
	LOADING_TIME_PROFILE_SECTION;
	uint32 i;

	for (i = 0; i < m_RContexts.Num(); i++)
	{
		if (m_RContexts[i]->m_hWnd == hWnd)
			break;
	}
	if (i != m_RContexts.Num())
		return true;
	SD3DContext* pContext = new SD3DContext;
	pContext->m_hWnd = (HWND)hWnd;
	pContext->m_X = 0;
	pContext->m_Y = 0;
	pContext->m_Width = m_width;
	pContext->m_Height = m_height;
	pContext->m_pSwapChain = 0;
	pContext->m_pBackBuffer = 0;
	pContext->m_pBackBuffers.clear();
	pContext->m_pCurrentBackBufferIndex = 0;
	pContext->m_nViewportWidth = m_width / (m_CurrContext ? m_CurrContext->m_nSSSamplesX : 1);
	pContext->m_nViewportHeight = m_height / (m_CurrContext ? m_CurrContext->m_nSSSamplesY : 1);
	pContext->m_nSSSamplesX = std::max(1, SSX);
	pContext->m_nSSSamplesY = std::max(1, SSY);
	pContext->m_bMainViewport = bMainViewport;
	m_CurrContext = pContext;
	m_RContexts.AddElem(pContext);

	return true;
}

bool CD3D9Renderer::DeleteContext(WIN_HWND hWnd)
{
	uint32 i, j;

	for (i = 0; i < m_RContexts.Num(); i++)
	{
		if (m_RContexts[i]->m_hWnd == hWnd)
			break;
	}
	if (i == m_RContexts.Num())
		return false;
	if (m_CurrContext == m_RContexts[i])
	{
		for (j = 0; j < m_RContexts.Num(); j++)
		{
			if (m_RContexts[j]->m_hWnd != hWnd)
			{
				m_CurrContext = m_RContexts[j];
				break;
			}
		}
		if (j == m_RContexts.Num())
			m_CurrContext = NULL;

		if (!m_CurrContext)
		{
			m_width = 0;
			m_height = 0;
		}
		else if (m_CurrContext->m_Width != m_width || m_CurrContext->m_Height != m_height)
		{
			m_width = m_CurrContext->m_Width;
			m_height = m_CurrContext->m_Height;
		}
	}

	SAFE_RELEASE(m_RContexts[i]->m_pSwapChain);

	delete m_RContexts[i];
	m_RContexts.Remove(i, 1);

	return true;
}

void CD3D9Renderer::MakeMainContextActive()
{
	if (m_RContexts.empty() || m_CurrContext == m_RContexts[0])
		return;

	m_CurrContext = m_RContexts[0];

	CHWShader::s_pCurVS = NULL;
	CHWShader::s_pCurPS = NULL;
}

bool CD3D9Renderer::CreateMSAADepthBuffer()
{
	CD3D9Renderer* rd = gcpRendD3D;
	HRESULT hr = S_OK;
	if (CV_r_msaa)
	{
		if (m_RP.m_MSAAData.Type != CV_r_msaa_samples ||
		    m_RP.m_MSAAData.Quality != CV_r_msaa_quality)
		{
		m_RP.m_MSAAData.Type = CV_r_msaa_samples;
		m_RP.m_MSAAData.Quality = CV_r_msaa_quality;

			const float clearDepth = CRenderer::CV_r_ReverseDepth ? 0.f : 1.f;
			const uint clearStencil = 1;
			const ColorF clearValues = ColorF(clearDepth, FLOAT(clearStencil), 0.f, 0.f);

			rd->m_pZTextureMSAA = CTexture::CreateRenderTarget("$DeviceDepthMSAA", m_width, m_height,
				clearValues, eTT_2D, FT_USAGE_MSAA | FT_USAGE_DEPTHSTENCIL | FT_DONT_RELEASE | FT_DONT_STREAM, rd->m_zbpp == 32 ? eTF_D32FS8 : eTF_D24S8);

			m_RP.m_MSAAData.m_pZTexture = rd->m_pZTextureMSAA;
			m_DepthBufferOrigMSAA.pTexture = m_RP.m_MSAAData.m_pZTexture;
			m_DepthBufferOrigMSAA.pTarget = m_RP.m_MSAAData.m_pZTexture->GetDevTexture()->Get2DTexture();
			m_DepthBufferOrigMSAA.pSurface = m_RP.m_MSAAData.m_pZTexture->GetDeviceDepthStencilView(0, -1, true, false);
		}
	}
	else
	{
		m_RP.m_MSAAData.Type = 0;
		m_RP.m_MSAAData.Quality = 0;
		m_DepthBufferOrigMSAA = m_DepthBufferOrig;
	}

	return (hr == S_OK);
}

#if defined(SUPPORT_DEVICE_INFO_USER_DISPLAY_OVERRIDES)
static void UserOverrideDXGIOutputFS(DeviceInfo& devInfo, int outputIndex, int defaultX, int defaultY, int& outputX, int& outputY)
{
	outputX = defaultX;
	outputY = defaultY;

	// This is not an ideal solution. Just for development or careful use.
	// The FS output override might be incompatible with output originally set up in device info.
	// As such selected resolutions might not be directly supported but currently won't fall back properly.
	#if CRY_PLATFORM_WINDOWS
	if (outputIndex > 0)
	{
		bool success = false;

		IDXGIOutput* pOutput = 0;
		if (SUCCEEDED(devInfo.Adapter()->EnumOutputs(outputIndex, &pOutput)) && pOutput)
		{
			DXGI_OUTPUT_DESC outputDesc;
			if (SUCCEEDED(pOutput->GetDesc(&outputDesc)))
			{
				MONITORINFO monitorInfo;
				monitorInfo.cbSize = sizeof(monitorInfo);
				if (GetMonitorInfo(outputDesc.Monitor, &monitorInfo))
				{
					outputX = monitorInfo.rcMonitor.left;
					outputY = monitorInfo.rcMonitor.top;
					success = true;
				}
			}
		}
		SAFE_RELEASE(pOutput);

		if (!success)
			CryLogAlways("Failed to resolve DXGI display for override index %d. Falling back to preferred or primary display.", outputIndex);
	}
	#endif
}
#endif

bool CD3D9Renderer::ChangeResolution(int nNewWidth, int nNewHeight, int nNewColDepth, int nNewRefreshHZ, bool bFullScreen, bool bForceReset)
{
	if (m_bDeviceLost)
		return true;

#if !defined(_RELEASE) && (CRY_PLATFORM_WINDOWS || CRY_PLATFORM_APPLE || CRY_PLATFORM_LINUX || CRY_PLATFORM_ANDROID)
	if (m_pRT && !m_pRT->IsRenderThread()) __debugbreak();
#endif

	iLog->Log("Changing resolution...");

	const int nPrevWidth = m_width;
	const int nPrevHeight = m_height;
	const int nPrevColorDepth = m_cbpp;
	const bool bPrevFullScreen = m_bFullScreen;
	if (nNewColDepth < 24)
		nNewColDepth = 16;
	else
		nNewColDepth = 32;
	bool bNeedReset = bForceReset || nNewColDepth != nPrevColorDepth || bFullScreen != bPrevFullScreen || nNewWidth != nPrevWidth || nNewHeight != nPrevHeight;
#if !defined(SUPPORT_DEVICE_INFO)
	bNeedReset |= m_VSync != CV_r_vsync;
#endif

#if defined(SUPPORT_DEVICE_INFO_USER_DISPLAY_OVERRIDES)
	bNeedReset |= m_overrideRefreshRate != CV_r_overrideRefreshRate || m_overrideScanlineOrder != CV_r_overrideScanlineOrder;
#endif

	GetS3DRend().ReleaseBuffers();
	DeleteContext(m_hWnd);

	// Save the new dimensions
	m_width = nNewWidth;
	m_height = nNewHeight;
	m_cbpp = nNewColDepth;
	m_bFullScreen = bFullScreen;
#if defined(SUPPORT_DEVICE_INFO_USER_DISPLAY_OVERRIDES)
	m_overrideRefreshRate = CV_r_overrideRefreshRate;
	m_overrideScanlineOrder = CV_r_overrideScanlineOrder;
#endif
	if (!IsEditorMode())
		m_VSync = CV_r_vsync;
	else
		m_VSync = 0;
#if defined(SUPPORT_DEVICE_INFO)
	m_devInfo.SyncInterval() = m_VSync ? 1 : 0;
#endif

	if (bFullScreen && nNewColDepth == 16)
	{
		m_zbpp = 16;
		m_sbpp = 0;
	}

	RestoreGamma();

	bool bFullscreenWindow = false;
#if CRY_PLATFORM_WINDOWS
	bFullscreenWindow = CV_r_FullscreenWindow && CV_r_FullscreenWindow->GetIVal() != 0;
#endif

	if (IsEditorMode() && !bForceReset)
	{
		nNewWidth = m_deskwidth;
		nNewHeight = m_deskheight;
	}
	HRESULT hr = S_OK;
	if (bNeedReset)
	{
#if defined(SUPPORT_DEVICE_INFO)
	#if CRY_PLATFORM_WINDOWS
		// disable floating point exceptions due to driver bug when switching to fullscreen
		SCOPED_DISABLE_FLOAT_EXCEPTIONS();
	#endif
	#if defined(CRY_USE_DX12)
		CRY_ASSERT_MESSAGE(!(bFullScreen && (m_devInfo.SwapChainDesc().Flags & DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT)),
			"Fullscreen does not work with Waitable SwapChain");
	#endif
		
		m_devInfo.SwapChainDesc().Windowed = !bFullScreen;
		m_devInfo.SwapChainDesc().BufferDesc.Width = m_backbufferWidth;
		m_devInfo.SwapChainDesc().BufferDesc.Height = m_backbufferHeight;

		m_devInfo.SnapSettings();

		int swapChainWidth = m_devInfo.SwapChainDesc().BufferDesc.Width;
		int swapChainHeight = m_devInfo.SwapChainDesc().BufferDesc.Height;
		if (m_backbufferWidth != swapChainWidth || m_backbufferHeight != swapChainHeight)
		{
			if (m_nativeWidth == m_backbufferWidth)
			{
				if (m_width == m_nativeWidth)
				{
					m_width = swapChainWidth;
					if (m_CVWidth)
						m_CVWidth->Set(swapChainWidth);
				}
				m_nativeWidth = swapChainWidth;
			}
			m_backbufferWidth = swapChainWidth;

			if (m_nativeHeight == m_backbufferHeight)
			{
				if (m_height == m_nativeHeight)
				{
					m_height = swapChainHeight;
					if (m_CVHeight)
						m_CVHeight->Set(swapChainHeight);
				}
				m_nativeHeight = swapChainHeight;
			}
			m_backbufferHeight = swapChainHeight;
		}

		AdjustWindowForChange();

	#if defined(SUPPORT_DEVICE_INFO_USER_DISPLAY_OVERRIDES)
		UserOverrideDisplayProperties(m_devInfo.SwapChainDesc().BufferDesc);
	#endif
	#if DURANGO_ENABLE_ASYNC_DIPS
		WaitForAsynchronousDevice();
	#endif

		D3DDepthSurface* pDSV = 0;
		D3DSurface* pRTVs[8] = { 0 };
		GetDeviceContext().OMSetRenderTargets(8, pRTVs, pDSV);

		ReleaseBackBuffers();

		if (bPrevFullScreen != bFullScreen)
		m_pSwapChain->SetFullscreenState(bFullScreen, 0);

		m_pSwapChain->ResizeTarget(&m_devInfo.SwapChainDesc().BufferDesc);
		m_devInfo.ResizeDXGIBuffers();

		OnD3D11PostCreateDevice(m_devInfo.Device());

		if (gEnv->pHardwareMouse)
			gEnv->pHardwareMouse->GetSystemEventListener()->OnSystemEvent(ESYSTEM_EVENT_TOGGLE_FULLSCREEN, bFullScreen ? 1 : 0, 0);
#endif
		m_FullResRect.right = m_width;
		m_FullResRect.bottom = m_height;

#if CRY_PLATFORM_WINDOWS || CRY_PLATFORM_LINUX || CRY_PLATFORM_ANDROID || CRY_PLATFORM_APPLE
		m_pRT->RC_SetViewport(0, 0, m_width, m_height);
#else
		RT_SetViewport(0, 0, m_width, m_height);
#endif
#if CRY_PLATFORM_ORBIS
		m_pSwapChain->UpdateBackbufferDimensions(m_backbufferWidth, m_backbufferHeight);
#endif
		m_MainViewport.nX = 0;
		m_MainViewport.nY = 0;
		m_MainViewport.nWidth = m_width;
		m_MainViewport.nHeight = m_height;
		m_MainRTViewport.nX = 0;
		m_MainRTViewport.nY = 0;
		m_MainRTViewport.nWidth = m_width;
		m_MainRTViewport.nHeight = m_height;
	}

	AdjustWindowForChange();

	GetS3DRend().OnResolutionChanged();

#if CRY_PLATFORM_WINDOWS
	SetWindowText(m_hWnd, m_WinTitle);
	iLog->Log("  Window resolution: %dx%dx%d (%s)", m_d3dsdBackBuffer.Width, m_d3dsdBackBuffer.Height, nNewColDepth, bFullScreen ? "Fullscreen" : "Windowed");
	iLog->Log("  Render resolution: %dx%d)", m_width, m_height);
#endif

	CreateMSAADepthBuffer();

	ICryFont* pCryFont = gEnv->pCryFont;
	if (pCryFont)
	{
		IFFont* pFont = pCryFont->GetFont("default");
	}

	PostDeviceReset();

	return true;
}

void CD3D9Renderer::PostDeviceReset()
{
	m_bDeviceLost = 0;
	if (m_bFullScreen)
		SetGamma(CV_r_gamma + m_fDeltaGamma, CV_r_brightness, CV_r_contrast, true);
	FX_ResetPipe();
	CHWShader::s_pCurVS = NULL;
	CHWShader::s_pCurPS = NULL;

	for (int i = 0; i < MAX_TMU; i++)
	{
		CTexture::s_TexStages[i].m_DevTexture = NULL;
	}
	m_nFrameReset++;
}

//-----------------------------------------------------------------------------
// Name: CD3D9Renderer::AdjustWindowForChange()
// Desc: Prepare the window for a possible change between windowed mode and
//       fullscreen mode.  This function is virtual and thus can be overridden
//       to provide different behavior, such as switching to an entirely
//       different window for fullscreen mode (as in the MFC sample apps).
//-----------------------------------------------------------------------------
HRESULT CD3D9Renderer::AdjustWindowForChange()
{

	if (m_windowParametersOverridden)
	{
		OverrideWindowParameters(true, m_overriddenWindowSize.x, m_overriddenWindowSize.y, m_overriddenWindowFullscreenState);
		return S_OK;
	}
#if CRY_PLATFORM_WINDOWS || defined(OPENGL)
	if (IsEditorMode())
		return S_OK;

	#if defined(OPENGL)
	const DXGI_SWAP_CHAIN_DESC& swapChainDesc(m_devInfo.SwapChainDesc());

	DXGI_MODE_DESC modeDesc;
	modeDesc.Width = m_backbufferWidth;
	modeDesc.Height = m_backbufferHeight;
	modeDesc.RefreshRate = swapChainDesc.BufferDesc.RefreshRate;
	modeDesc.Format = swapChainDesc.BufferDesc.Format;
	modeDesc.ScanlineOrdering = swapChainDesc.BufferDesc.ScanlineOrdering;
	modeDesc.Scaling = swapChainDesc.BufferDesc.Scaling;

	HRESULT result = m_pSwapChain->ResizeTarget(&modeDesc);
	if (FAILED(result))
		return result;
	#elif CRY_PLATFORM_WINDOWS
	bool bFullscreenWindow = CV_r_FullscreenWindow && CV_r_FullscreenWindow->GetIVal() != 0;

	if (!m_bFullScreen && !bFullscreenWindow)
	{
		// Set windowed-mode style
		SetWindowLongW(m_hWnd, GWL_STYLE, m_dwWindowStyle);
	}
	else
	{
		// Set fullscreen-mode style
		SetWindowLongW(m_hWnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
	}

	if (m_bFullScreen)
	{
		int x = m_prefMonX;
		int y = m_prefMonY;
		#if defined(SUPPORT_DEVICE_INFO_USER_DISPLAY_OVERRIDES)
		UserOverrideDXGIOutputFS(m_devInfo, CV_r_overrideDXGIOutputFS, m_prefMonX, m_prefMonY, x, y);
		#endif
		const int wdt = m_backbufferWidth;
		const int hgt = m_backbufferHeight;
		SetWindowPos(m_hWnd, HWND_TOPMOST, x, y, wdt, hgt, SWP_SHOWWINDOW);
	}
	else if (bFullscreenWindow)
	{
		const int x = m_prefMonX + (m_prefMonWidth - m_backbufferWidth) / 2;
		const int y = m_prefMonY + (m_prefMonHeight - m_backbufferHeight) / 2;
		const int wdt = m_backbufferWidth;
		const int hgt = m_backbufferHeight;
		SetWindowPos(m_hWnd, HWND_NOTOPMOST, x, y, wdt, hgt, SWP_SHOWWINDOW);
	}
	else
	{
		RECT wndrect;
		SetRect(&wndrect, 0, 0, m_backbufferWidth, m_backbufferHeight);
		AdjustWindowRectEx(&wndrect, m_dwWindowStyle, FALSE, WS_EX_APPWINDOW);

		const int wdt = wndrect.right - wndrect.left;
		const int hgt = wndrect.bottom - wndrect.top;

		const int x = m_prefMonX + (m_prefMonWidth - wdt) / 2;
		const int y = m_prefMonY + (m_prefMonHeight - hgt) / 2;

		SetWindowPos(m_hWnd, HWND_NOTOPMOST, x, y, wdt, hgt, SWP_SHOWWINDOW);
	}
	#endif

	//set viewport to ensure we have a valid one, even when doing chainloading
	// and playing a video before going ingame
	m_MainViewport.nX = 0;
	m_MainViewport.nY = 0;
	m_MainViewport.nWidth = m_width;
	m_MainViewport.nHeight = m_height;
	m_MainRTViewport.nX = 0;
	m_MainRTViewport.nY = 0;
	m_MainRTViewport.nWidth = m_width;
	m_MainRTViewport.nHeight = m_height;

	m_FullResRect.right = m_width;
	m_FullResRect.bottom = m_height;

	m_pRT->RC_SetViewport(0, 0, m_width, m_height);
#endif
	return S_OK;
}

//override window parameters, mostly taken from AdjustWindowForChange
void CD3D9Renderer::OverrideWindowParameters(bool overrideParameters, int width, int height, bool fullscreen)
{

	m_windowParametersOverridden = overrideParameters;
	if (!m_windowParametersOverridden)
	{
		AdjustWindowForChange();
		return;
	}

#if CRY_PLATFORM_WINDOWS
	if (width == 0 || height == 0 || fullscreen)
	{
		width = m_prefMonWidth;
		height = m_prefMonHeight;
	}

	if (!fullscreen)
	{
		SetWindowLongW(m_hWnd, GWL_STYLE, m_dwWindowStyle);
	}
	else
	{
		// Set fullscreen-mode style
		SetWindowLongW(m_hWnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
	}

	if (fullscreen)
	{
		int x = m_prefMonX;
		int y = m_prefMonY;
		SetWindowPos(m_hWnd, HWND_TOPMOST, x, y, width, height, SWP_SHOWWINDOW);
	}
	else
	{
		RECT wndrect;
		SetRect(&wndrect, 0, 0, width, height);
		AdjustWindowRectEx(&wndrect, m_dwWindowStyle, FALSE, WS_EX_APPWINDOW);

		const int wdt = wndrect.right - wndrect.left;
		const int hgt = wndrect.bottom - wndrect.top;

		const int x = m_prefMonX + (m_prefMonWidth - wdt) / 2;
		const int y = m_prefMonY + (m_prefMonHeight - hgt) / 2;

		SetWindowPos(m_hWnd, HWND_NOTOPMOST, x, y, wdt, hgt, SWP_SHOWWINDOW);
	}

	m_overriddenWindowSize.x = width;
	m_overriddenWindowSize.y = height;
	m_overriddenWindowFullscreenState = fullscreen;
#endif
}

#if defined(SUPPORT_DEVICE_INFO)
bool compareDXGIMODEDESC(const DXGI_MODE_DESC& lhs, const DXGI_MODE_DESC& rhs)
{
	if (lhs.Width != rhs.Width)
		return lhs.Width < rhs.Width;
	return lhs.Height < rhs.Height;
}
#endif

int CD3D9Renderer::EnumDisplayFormats(SDispFormat* formats)
{
#if CRY_PLATFORM_WINDOWS || defined(OPENGL)

	#if defined(SUPPORT_DEVICE_INFO)

	unsigned int numModes = 0;
	if (SUCCEEDED(m_devInfo.Output()->GetDisplayModeList(m_devInfo.SwapChainDesc().BufferDesc.Format, 0, &numModes, 0)) && numModes)
	{
		std::vector<DXGI_MODE_DESC> dispModes(numModes);
		if (SUCCEEDED(m_devInfo.Output()->GetDisplayModeList(m_devInfo.SwapChainDesc().BufferDesc.Format, 0, &numModes, &dispModes[0])) && numModes)
		{

			std::sort(dispModes.begin(), dispModes.end(), compareDXGIMODEDESC);

			unsigned int numUniqueModes = 0;
			unsigned int prevWidth = 0;
			unsigned int prevHeight = 0;
			for (unsigned int i = 0; i < numModes; ++i)
			{
				if (prevWidth != dispModes[i].Width || prevHeight != dispModes[i].Height)
				{
					if (formats)
					{
						formats[numUniqueModes].m_Width = dispModes[i].Width;
						formats[numUniqueModes].m_Height = dispModes[i].Height;
						formats[numUniqueModes].m_BPP = CTexture::BitsPerPixel(CTexture::TexFormatFromDeviceFormat(dispModes[i].Format));
					}

					prevWidth = dispModes[i].Width;
					prevHeight = dispModes[i].Height;
					++numUniqueModes;
				}
			}

			numModes = numUniqueModes;
		}
	}

	return numModes;

	#endif

#else
	return 0;
#endif
}

bool CD3D9Renderer::ChangeDisplay(unsigned int width, unsigned int height, unsigned int cbpp)
{
	return false;
}

void CD3D9Renderer::UnSetRes()
{
	m_Features |= RFT_SUPPORTZBIAS;

#if defined(SUPPORT_D3D_DEBUG_RUNTIME)
	m_d3dDebug.Release();
#endif
}

void CD3D9Renderer::DestroyWindow(void)
{
#if defined(DXGL_USE_SDL)
	DXGLDestroySDLWindow(m_hWnd);
#elif CRY_PLATFORM_WINDOWS
	if (gEnv && gEnv->pSystem)
	{
		gEnv->pSystem->UnregisterWindowMessageHandler(this);
	}
	if (m_hWnd)
	{
		::DestroyWindow(m_hWnd);
		m_hWnd = NULL;
	}
	if (m_hIconBig)
	{
		::DestroyIcon(m_hIconBig);
		m_hIconBig = NULL;
	}
	if (m_hIconSmall)
	{
		::DestroyIcon(m_hIconSmall);
		m_hIconSmall = NULL;
	}
#endif
}

static CD3D9Renderer::SGammaRamp orgGamma;

static BOOL g_doGamma = false;

void CD3D9Renderer::RestoreGamma(void)
{
	//if (!m_bFullScreen)
	//	return;

	if (!(GetFeatures() & RFT_HWGAMMA))
		return;

	if (CV_r_nohwgamma)
		return;

	m_fLastGamma = 1.0f;
	m_fLastBrightness = 0.5f;
	m_fLastContrast = 0.5f;

	//iLog->Log("...RestoreGamma");

#if CRY_PLATFORM_WINDOWS
	if (!g_doGamma)
		return;

	g_doGamma = false;

	m_hWndDesktop = GetDesktopWindow();

	if (HDC dc = GetDC(m_hWndDesktop))
	{
		SetDeviceGammaRamp(dc, &orgGamma);
		ReleaseDC(m_hWndDesktop, dc);
	}
#endif
}

void CD3D9Renderer::GetDeviceGamma()
{
#if CRY_PLATFORM_WINDOWS
	if (g_doGamma)
	{
		return;
	}

	m_hWndDesktop = GetDesktopWindow();

	if (HDC dc = GetDC(m_hWndDesktop))
	{
		g_doGamma = true;

		if (!GetDeviceGammaRamp(dc, &orgGamma))
		{
			for (uint16 i = 0; i < 256; i++)
			{
				orgGamma.red[i] = i * 0x101;
				orgGamma.green[i] = i * 0x101;
				orgGamma.blue[i] = i * 0x101;
			}
		}

		ReleaseDC(m_hWndDesktop, dc);
	}
#endif
}

void CD3D9Renderer::SetDeviceGamma(SGammaRamp* gamma)
{
	if (!(GetFeatures() & RFT_HWGAMMA))
		return;

	if (CV_r_nohwgamma)
		return;

#if CRY_PLATFORM_WINDOWS
	if (!g_doGamma)
		return;

	m_hWndDesktop = GetDesktopWindow();  // TODO: DesktopWindow - does not represent actual output window thus gamma affects all desktop monitors !!!

	if (HDC dc = GetDC(m_hWndDesktop))
	{
		g_doGamma = true;
		// INFO!!! - very strange: in the same time
		// GetDeviceGammaRamp -> TRUE
		// SetDeviceGammaRamp -> FALSE but WORKS!!!
		// at least for desktop window DC... be careful
		SetDeviceGammaRamp(dc, gamma);
		ReleaseDC(m_hWndDesktop, dc);
	}
#endif
}

////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////

void CD3D9Renderer::SetGamma(float fGamma, float fBrightness, float fContrast, bool bForce)
{
	if (m_pStereoRenderer)  // Function can be called on shutdown
		fGamma += GetS3DRend().GetGammaAdjustment();

	fGamma = CLAMP(fGamma, 0.4f, 1.6f);

	if (!bForce && m_fLastGamma == fGamma && m_fLastBrightness == fBrightness && m_fLastContrast == fContrast)
		return;

	GetDeviceGamma();

	SGammaRamp gamma;

	float fInvGamma = 1.f / fGamma;

	float fAdd = (fBrightness - 0.5f) * 0.5f - fContrast * 0.5f + 0.25f;
	float fMul = fContrast + 0.5f;

	for (int i = 0; i < 256; i++)
	{
		float pfInput[3];

		pfInput[0] = (float)(orgGamma.red[i] >> 8) / 255.f;
		pfInput[1] = (float)(orgGamma.green[i] >> 8) / 255.f;
		pfInput[2] = (float)(orgGamma.blue[i] >> 8) / 255.f;

		pfInput[0] = pow_tpl(pfInput[0], fInvGamma) * fMul + fAdd;
		pfInput[1] = pow_tpl(pfInput[1], fInvGamma) * fMul + fAdd;
		pfInput[2] = pow_tpl(pfInput[2], fInvGamma) * fMul + fAdd;

		gamma.red[i] = CLAMP(int_round(pfInput[0] * 65535.f), 0, 65535);
		gamma.green[i] = CLAMP(int_round(pfInput[1] * 65535.f), 0, 65535);
		gamma.blue[i] = CLAMP(int_round(pfInput[2] * 65535.f), 0, 65535);
	}

	SetDeviceGamma(&gamma);

	m_fLastGamma = fGamma;
	m_fLastBrightness = fBrightness;
	m_fLastContrast = fContrast;
}

bool CD3D9Renderer::SetGammaDelta(const float fGamma)
{
	m_fDeltaGamma = fGamma;
	SetGamma(CV_r_gamma + fGamma, CV_r_brightness, CV_r_contrast, false);
	return true;
}

SDepthTexture::~SDepthTexture()
{
}

void SDepthTexture::Release(bool bReleaseTexture)
{
	if (bReleaseTexture && pTexture)
		pTexture->Release();
	
	pTarget = nullptr;
	pSurface = nullptr;
	pTexture = nullptr;
}

void CD3D9Renderer::ShutDownFast()
{
	// Flush RT command buffer
	ForceFlushRTCommands();
	CHWShader::mfFlushPendedShadersWait(-1);
	FX_PipelineShutdown(true);
	//CBaseResource::ShutDown();
	memset(&CTexture::s_TexStages[0], 0, sizeof(CTexture::s_TexStages));
	for (uint32 i = 0; i < CTexture::s_TexStates.size(); i++)
	{
		memset(&CTexture::s_TexStates[i], 0, sizeof(STexState));
	}
	SAFE_DELETE(m_pRT);

#if defined(OPENGL)
	#if !DXGL_FULL_EMULATION && !CRY_OPENGL_SINGLE_CONTEXT
	if (CV_r_multithreaded)
		DXGLReleaseContext(m_devInfo.Device());
	#endif
	m_devInfo.Release();
#endif
}

void CD3D9Renderer::RT_ShutDown(uint32 nFlags)
{
	SAFE_RELEASE(m_pZTexture);
	SAFE_RELEASE(m_pZTextureMSAA);
	SAFE_RELEASE(m_pNativeZTexture);

#if defined(INCLUDE_SCALEFORM_SDK) || defined(CRY_FEATURE_SCALEFORM_HELPER)
	SF_DestroyResources();
#endif

	CREBreakableGlassBuffer::RT_ReleaseInstance();
	SAFE_DELETE(m_pColorGradingControllerD3D);
	SAFE_DELETE(m_pPostProcessMgr);
	SAFE_DELETE(m_pWaterSimMgr);
	SAFE_DELETE(m_pStereoRenderer);
	SAFE_DELETE(m_pPipelineProfiler);
#if defined(ENABLE_RENDER_AUX_GEOM)
	SAFE_DELETE(m_pRenderAuxGeomD3D);
#endif

	for (size_t i = 0; i < 3; ++i)
		while (m_CharCBActiveList[i].next != &m_CharCBActiveList[i])
			delete m_CharCBActiveList[i].next->item<& SCharacterInstanceCB::list>();
	while (m_CharCBFreeList.next != &m_CharCBFreeList)
		delete m_CharCBFreeList.next->item<& SCharacterInstanceCB::list>();

	for (uint32 i = 0; i < CRY_ARRAY_COUNT(m_frameFences); i++)
		m_DevMan.ReleaseFence(m_frameFences[i]);

	CHWShader::mfFlushPendedShadersWait(-1);
	if (nFlags == FRR_ALL)
	{
		memset(&CTexture::s_TexStages[0], 0, sizeof(CTexture::s_TexStages));
		CTexture::s_TexStates.clear();
		FreeResources(FRR_ALL);
	}

	FX_PipelineShutdown();

#if defined(SUPPORT_DEVICE_INFO)
	//m_devInfo.Release();
	#if defined(OPENGL) && !DXGL_FULL_EMULATION
		#if CRY_OPENGL_SINGLE_CONTEXT
	m_pRT->m_kDXGLDeviceContextHandle.Set(NULL);
		#else
	m_pRT->m_kDXGLDeviceContextHandle.Set(NULL, !CV_r_multithreaded);
	m_pRT->m_kDXGLContextHandle.Set(NULL);
		#endif
	#endif
#endif

#if !CRY_PLATFORM_ORBIS && !defined(OPENGL)
	GetDeviceContext().ReleaseDeviceContext();
#endif

#if defined(ENABLE_NULL_D3D11DEVICE)
	if (m_bShaderCacheGen)
		GetDevice().ReleaseDevice();
#endif
}

void CD3D9Renderer::ShutDown(bool bReInit)
{
	m_bInShutdown = true;

	// Force Flush RT command buffer
	ForceFlushRTCommands();
	PreShutDown();
	if (m_pRT)
		m_pRT->RC_ShutDown(bReInit ? (FRR_SHADERS | FRR_TEXTURES | FRR_REINITHW) : FRR_ALL);

	//CBaseResource::ShutDown();
	ForceFlushRTCommands();

#ifdef USE_PIX_DURANGO
	SAFE_RELEASE(m_pPixPerf);
#endif
	//////////////////////////////////////////////////////////////////////////
	// Clear globals.
	//////////////////////////////////////////////////////////////////////////

	for (uint32 id = 0; id < RT_COMMAND_BUF_COUNT; ++id)
	{
		delete m_RP.m_TI[id].m_matView;
		delete m_RP.m_TI[id].m_matProj;
	}

	SAFE_DELETE(m_pRT);

#if defined(OPENGL)
	#if !DXGL_FULL_EMULATION && !CRY_OPENGL_SINGLE_CONTEXT
	if (CV_r_multithreaded)
		DXGLReleaseContext(GetDevice().GetRealDevice());
	#endif
	m_devInfo.Release();
#endif

	if (!bReInit)
	{
		iLog = NULL;
		//iConsole = NULL;
		iTimer = NULL;
		iSystem = NULL;
	}

	PostShutDown();
}

#if CRY_PLATFORM_WINDOWS
LRESULT CALLBACK LowLevelKeyboardProc(INT nCode, WPARAM wParam, LPARAM lParam)
{
	KBDLLHOOKSTRUCT* pkbhs = (KBDLLHOOKSTRUCT*) lParam;
	BOOL bControlKeyDown = 0;
	switch (nCode)
	{
	case HC_ACTION:
		{
			if (pkbhs->vkCode == VK_TAB && pkbhs->flags & LLKHF_ALTDOWN)
				return 1;            // Disable ALT+ESC
		}
	default:
		break;
	}
	return CallNextHookEx(0, nCode, wParam, lParam);
}
#endif

#if defined(SUPPORT_DEVICE_INFO)
HWND CD3D9Renderer::CreateWindowCallback()
{
	return gcpRendD3D->SetWindow(gcpRendD3D->GetBackbufferWidth(), gcpRendD3D->GetBackbufferHeight(), gcpRendD3D->m_bFullScreen, gcpRendD3D->m_hWnd) ? gcpRendD3D->m_hWnd : 0;
}
#endif

bool CD3D9Renderer::SetWindow(int width, int height, bool fullscreen, WIN_HWND hWnd)
{
	LOADING_TIME_PROFILE_SECTION;

	iSystem->RegisterWindowMessageHandler(this);

#if defined(DXGL_USE_SDL)
	DXGLCreateSDLWindow(m_WinTitle, width, height, fullscreen, &m_hWnd);
#elif CRY_PLATFORM_WINDOWS
	DWORD style, exstyle;
	int x, y, wdt, hgt;

	if (width < 640)
		width = 640;
	if (height < 480)
		height = 480;

	m_dwWindowStyle = WS_OVERLAPPEDWINDOW | WS_VISIBLE;

	// Do not allow the user to resize the window
	m_dwWindowStyle &= ~WS_MAXIMIZEBOX;
	m_dwWindowStyle &= ~WS_THICKFRAME;

	bool bFullscreenWindow = false;
	#if CRY_PLATFORM_WINDOWS
	bFullscreenWindow = CV_r_FullscreenWindow && CV_r_FullscreenWindow->GetIVal() != 0;
	#endif

	if (fullscreen || bFullscreenWindow)
	{
		exstyle = bFullscreenWindow ? WS_EX_APPWINDOW : WS_EX_TOPMOST;
		style = WS_POPUP | WS_VISIBLE;
		x = m_prefMonX + (m_prefMonWidth - width) / 2;
		y = m_prefMonY + (m_prefMonHeight - height) / 2;
		wdt = width;
		hgt = height;
	}
	else
	{
		exstyle = WS_EX_APPWINDOW;
		style = m_dwWindowStyle;

		RECT wndrect;
		SetRect(&wndrect, 0, 0, width, height);
		AdjustWindowRectEx(&wndrect, style, FALSE, exstyle);

		wdt = wndrect.right - wndrect.left;
		hgt = wndrect.bottom - wndrect.top;

		x = m_prefMonX + (m_prefMonWidth - wdt) / 2;
		y = m_prefMonY + (m_prefMonHeight - hgt) / 2;
	}

	if (IsEditorMode())
	{
	#if defined(UNICODE) || defined(_UNICODE)
		#error Review this, probably should be wide if Editor also has UNICODE support (or maybe moved into Editor)
	#endif
		m_dwWindowStyle = WS_OVERLAPPED;
		style = m_dwWindowStyle;
		exstyle = 0;
		x = y = 0;
		wdt = 100;
		hgt = 100;

		WNDCLASSA wc;
		memset(&wc, 0, sizeof(WNDCLASSA));
		wc.style = CS_OWNDC;
		wc.lpfnWndProc = DefWindowProcA;
		wc.hInstance = m_hInst;
		wc.lpszClassName = "D3DDeviceWindowClassForSandbox";
		if (!RegisterClassA(&wc))
		{
			CryFatalError("Cannot Register Window Class %s", wc.lpszClassName);
			return false;
		}
		m_hWnd = CreateWindowExA(exstyle, wc.lpszClassName, m_WinTitle, style, x, y, wdt, hgt, NULL, NULL, m_hInst, NULL);
		ShowWindow(m_hWnd, SW_HIDE);
	}
	else
	{
		if (!hWnd)
		{
			LPCWSTR pClassName = L"CryENGINE";

			// Load default icon if we have nothing yet
			if (m_hIconBig == NULL)
			{
				SetWindowIcon(gEnv->pConsole->GetCVar("r_WindowIconTexture")->GetString());
			}

			// Moved from Game DLL
			WNDCLASSEXW wc;
			memset(&wc, 0, sizeof(WNDCLASSEXW));
			wc.cbSize = sizeof(WNDCLASSEXW);
			wc.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
			wc.lpfnWndProc = (WNDPROC)GetISystem()->GetRootWindowMessageHandler();
			wc.hInstance = m_hInst;
			wc.hIcon = m_hIconBig;
			wc.hIconSm = m_hIconSmall;
			wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
			wc.lpszClassName = pClassName;
			if (!RegisterClassExW(&wc))
			{
				CryFatalError("Cannot Register Launcher Window Class");
				return false;
			}

			wstring wideTitle = Unicode::Convert<wstring>(m_WinTitle);

			m_hWnd = CreateWindowExW(exstyle, pClassName, wideTitle.c_str(), style, x, y, wdt, hgt, NULL, NULL, m_hInst, NULL);
			if (m_hWnd && !IsWindowUnicode(m_hWnd))
			{
				CryFatalError("Expected an UNICODE window for launcher");
				return false;
			}

			EnableCloseButton(m_hWnd, false);

			if (fullscreen && (!gEnv->pSystem->IsDevMode() && CV_r_enableAltTab == 0))
				SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc, NULL, 0);
		}
		else
			m_hWnd = (HWND)hWnd;

		ShowWindow(m_hWnd, SW_SHOWNORMAL);
		SetFocus(m_hWnd);
		SetForegroundWindow(m_hWnd);
	}

	if (!m_hWnd)
		iConsole->Exit("Couldn't create window\n");
#else
	return false;
#endif
	return true;
}

bool CD3D9Renderer::SetWindowIcon(const char* path)
{
#if CRY_PLATFORM_WINDOWS
	if (IsEditorMode())
	{
		return false;
	}

	if (stricmp(path, m_iconPath.c_str()) == 0)
	{
		return true;
	}

	HICON hIconBig = CreateResourceFromTexture(this, path, eResourceType_IconBig);
	if (hIconBig)
	{
		if (m_hWnd)
		{
			SendMessage(m_hWnd, WM_SETICON, ICON_BIG, (LPARAM)hIconBig);
		}
		if (m_hIconBig)
		{
			::DestroyIcon(m_hIconBig);
		}
		m_hIconBig = hIconBig;
		m_iconPath = path;
	}

	// Note: Also set the small icon manually.
	// Even though the big icon will also affect the small icon, the re-scaling done by GDI has aliasing problems.
	// Just grabbing a smaller MIP from the texture (if possible) will solve this.
	HICON hIconSmall = CreateResourceFromTexture(this, path, eResourceType_IconSmall);
	if (hIconSmall)
	{
		if (m_hWnd)
		{
			SendMessage(m_hWnd, WM_SETICON, ICON_SMALL, (LPARAM)hIconSmall);
		}
		if (m_hWnd)
		{
			SendMessage(m_hWnd, WM_SETICON, ICON_SMALL, (LPARAM)hIconSmall);
		}
		if (m_hIconSmall)
		{
			::DestroyIcon(m_hIconSmall);
		}
		m_hIconSmall = hIconSmall;
	}

	// Update the CVar value to match, in case this API was not called through CVar-change-callback
	gEnv->pConsole->GetCVar("r_WindowIconTexture")->Set(m_iconPath.c_str());

	return hIconBig != NULL;
#else
	return false;
#endif
}

#define QUALITY_VAR(name)                                                 \
  static void OnQShaderChange_Shader ## name(ICVar * pVar)                \
  {                                                                       \
    int iQuality = eSQ_Low;                                               \
    if (gRenDev->GetFeatures() & (RFT_HW_SM2X | RFT_HW_SM30))             \
      iQuality = CLAMP(pVar->GetIVal(), 0, eSQ_Max);                      \
    gRenDev->EF_SetShaderQuality(eST_ ## name, (EShaderQuality)iQuality); \
  }

QUALITY_VAR(General)
QUALITY_VAR(Metal)
QUALITY_VAR(Glass)
QUALITY_VAR(Vegetation)
QUALITY_VAR(Ice)
QUALITY_VAR(Terrain)
QUALITY_VAR(Shadow)
QUALITY_VAR(Water)
QUALITY_VAR(FX)
QUALITY_VAR(PostProcess)
QUALITY_VAR(HDR)
QUALITY_VAR(Sky)

#undef QUALITY_VAR

static void OnQShaderChange_Renderer(ICVar* pVar)
{
	int iQuality = eRQ_Low;

	if (gRenDev->GetFeatures() & (RFT_HW_SM2X | RFT_HW_SM30))
		iQuality = CLAMP(pVar->GetIVal(), 0, eSQ_Max);
	else
		pVar->ForceSet("0");

	gRenDev->m_RP.m_eQuality = (ERenderQuality)iQuality;
}

static void Command_Quality(IConsoleCmdArgs* Cmd)
{
	bool bLog = false;
	bool bSet = false;

	int iQuality = -1;

	if (Cmd->GetArgCount() == 2)
	{
		iQuality = CLAMP(atoi(Cmd->GetArg(1)), 0, eSQ_Max);
		bSet = true;
	}
	else bLog = true;

	if (bLog) iLog->LogWithType(IMiniLog::eInputResponse, " ");
	if (bLog) iLog->LogWithType(IMiniLog::eInputResponse, "Current quality settings (0=low/1=med/2=high/3=very high):");

#define QUALITY_VAR(name) if (bLog) iLog->LogWithType(IMiniLog::eInputResponse, "  $3q_" # name " = $6%d", gEnv->pConsole->GetCVar("q_" # name)->GetIVal()); \
  if (bSet) gEnv->pConsole->GetCVar("q_" # name)->Set(iQuality);

	QUALITY_VAR(ShaderGeneral)
	QUALITY_VAR(ShaderMetal)
	QUALITY_VAR(ShaderGlass)
	QUALITY_VAR(ShaderVegetation)
	QUALITY_VAR(ShaderIce)
	QUALITY_VAR(ShaderTerrain)
	QUALITY_VAR(ShaderShadow)
	QUALITY_VAR(ShaderWater)
	QUALITY_VAR(ShaderFX)
	QUALITY_VAR(ShaderPostProcess)
	QUALITY_VAR(ShaderHDR)
	QUALITY_VAR(ShaderSky)
	QUALITY_VAR(Renderer)

#undef QUALITY_VAR

	if (bSet) iLog->LogWithType(IMiniLog::eInputResponse, "Set quality to %d", iQuality);
}

const char* sGetSQuality(const char* szName)
{
	ICVar* pVar = iConsole->GetCVar(szName);
	assert(pVar);
	if (!pVar)
		return "Unknown";
	int iQ = pVar->GetIVal();
	switch (iQ)
	{
	case eSQ_Low:
		return "Low";
	case eSQ_Medium:
		return "Medium";
	case eSQ_High:
		return "High";
	case eSQ_VeryHigh:
		return "VeryHigh";
	default:
		return "Unknown";
	}
}

static void Command_ColorGradingChartImage(IConsoleCmdArgs* pCmd)
{
	CColorGradingControllerD3D* pCtrl = gcpRendD3D->m_pColorGradingControllerD3D;
	if (pCmd && pCtrl)
	{
		const int numArgs = pCmd->GetArgCount();
		if (numArgs == 1)
		{
			const CTexture* pChart = pCtrl->GetStaticColorChart();
			if (pChart)
				iLog->Log("current static chart is \"%s\"", pChart->GetName());
			else
				iLog->Log("no static chart loaded");
		}
		else if (numArgs == 2)
		{
			const char* pArg = pCmd->GetArg(1);
			if (pArg && pArg[0])
			{
				if (pArg[0] == '0' && !pArg[1])
				{
					pCtrl->LoadStaticColorChart(0);
					iLog->Log("static chart reset");
				}
				else
				{
					if (pCtrl->LoadStaticColorChart(pArg))
						iLog->Log("\"%s\" loaded successfully", pArg);
					else
						iLog->Log("failed to load \"%s\"", pArg);
				}
			}
		}
	}
}

WIN_HWND CD3D9Renderer::Init(int x, int y, int width, int height, unsigned int cbpp, int zbpp, int sbits, bool fullscreen, WIN_HINSTANCE hinst, WIN_HWND Glhwnd, bool bReInit, const SCustomRenderInitArgs* pCustomArgs, bool bShaderCacheGen)
{
	LOADING_TIME_PROFILE_SECTION;

	MEMSTAT_CONTEXT(EMemStatContextTypes::MSC_Other, 0, "Renderer initialisation");

	if (!iSystem || !iLog)
		return 0;

	iLog->Log("Initializing Direct3D and creating game window:");
	INDENT_LOG_DURING_SCOPE();

	m_CVWidth = iConsole->GetCVar("r_Width");
	m_CVHeight = iConsole->GetCVar("r_Height");
	m_CVFullScreen = iConsole->GetCVar("r_Fullscreen");
	m_CVDisplayInfo = iConsole->GetCVar("r_DisplayInfo");
	m_CVColorBits = iConsole->GetCVar("r_ColorBits");

	bool bNativeResolution;
#if CRY_PLATFORM_CONSOLE || CRY_PLATFORM_MOBILE
	bNativeResolution = true;
#elif CRY_PLATFORM_WINDOWS
	CV_r_FullscreenWindow = iConsole->GetCVar("r_FullscreenWindow");
	m_fullscreenWindow = CV_r_FullscreenWindow && CV_r_FullscreenWindow->GetIVal();
	CV_r_FullscreenNativeRes = iConsole->GetCVar("r_FullscreenNativeRes");
	bNativeResolution = CV_r_FullscreenNativeRes && CV_r_FullscreenNativeRes->GetIVal() != 0 && (fullscreen || m_fullscreenWindow);

	{
		RECT rcDesk;
		GetWindowRect(GetDesktopWindow(), &rcDesk);

		m_prefMonX = rcDesk.left;
		m_prefMonY = rcDesk.top;
		m_prefMonWidth = rcDesk.right - rcDesk.left;
		m_prefMonHeight = rcDesk.bottom - rcDesk.top;
	}
	{
		RECT rc;
		HDC hdc = GetDC(NULL);
		GetClipBox(hdc, &rc);
		ReleaseDC(NULL, hdc);
		m_deskwidth = rc.right - rc.left;
		m_deskheight = rc.bottom - rc.top;
	}

	REGISTER_STRING_CB("r_WindowIconTexture", "EngineAssets/Textures/default_icon.dds", VF_CHEAT | VF_CHEAT_NOCHECK,
	                   "Sets the image (dds file) to be displayed as the window and taskbar icon",
	                   SetWindowIconCVar);
#else
	bNativeResolution = false;
#endif

#if CRY_PLATFORM_DESKTOP
	REGISTER_STRING_CB("r_MouseCursorTexture", "EngineAssets/Textures/Cursor_Green.dds", VF_CHEAT | VF_CHEAT_NOCHECK,
	                   "Sets the image (dds file) to be displayed as the mouse cursor",
	                   SetMouseCursorIconCVar);
#endif

#if defined(OPENGL) && !DXGL_FULL_EMULATION
	#if CRY_OPENGL_SINGLE_CONTEXT
	DXGLInitialize(0);
	#else
	DXGLInitialize(CV_r_multithreaded ? 4 : 0);
	#endif
#endif //defined(OPENGL) && !DXGL_FULL_EMULATION

#ifdef D3DX_SDK_VERSION
	iLog->Log("D3DX_SDK_VERSION = %d", D3DX_SDK_VERSION);
#else
	iLog->Log("D3DX_SDK_VERSION = <UNDEFINED>");
#endif

	iLog->Log("Direct3D driver is creating...");
	iLog->Log("Crytek Direct3D driver version %4.2f (%s <%s>)", VERSION_D3D, __DATE__, __TIME__);

	const char* sGameName = iConsole->GetCVar("sys_game_name")->GetString();

#if defined(IS_EAAS)
	cry_sprintf(m_WinTitle, "%s [EaaS]", sGameName);
#else
	cry_strcpy(m_WinTitle, sGameName);
#endif

	iLog->Log("Creating window called '%s' (%dx%d)", m_WinTitle, width, height);

	m_hInst = (HINSTANCE)(TRUNCATE_PTR)hinst;

	if (Glhwnd == (WIN_HWND)1)
	{
		Glhwnd = 0;
		m_bEditor = true;
		fullscreen = false;
	}

	m_bShaderCacheGen = bShaderCacheGen;

	m_cbpp = cbpp;
	m_zbpp = zbpp;
	m_sbpp = sbits;
	m_bFullScreen = fullscreen;

	CalculateResolutions(width, height, bNativeResolution, &m_width, &m_height, &m_nativeWidth, &m_nativeHeight, &m_backbufferWidth, &m_backbufferHeight);

	// only create device if we are not in shader cache generation mode
	if (!m_bShaderCacheGen)
	{
		// call init stereo before device is created!
		m_pStereoRenderer->InitDeviceBeforeD3D();

		while (true)
		{
			m_hWnd = (HWND)Glhwnd;

			// Creates Device here.
			bool bRes = m_pRT->RC_CreateDevice();
			if (!bRes)
			{
				ShutDown(true);
				return 0;
			}

			break;
		}

#if defined(SUPPORT_DEVICE_INFO)
		iLog->Log(" ****** D3D11 CryRender Stats ******");
		iLog->Log(" Driver description: %S", m_devInfo.AdapterDesc().Description);

		switch (m_devInfo.FeatureLevel())
		{
		case D3D_FEATURE_LEVEL_9_1:
			iLog->Log(" Feature level: DirectX 9.1");
			break;
		case D3D_FEATURE_LEVEL_9_2:
			iLog->Log(" Feature level: DirectX 9.2");
			break;
		case D3D_FEATURE_LEVEL_9_3:
			iLog->Log(" Feature level: DirectX 9.3");
			break;
		case D3D_FEATURE_LEVEL_10_0:
			iLog->Log(" Feature level: DirectX 10.0");
			break;
		case D3D_FEATURE_LEVEL_10_1:
			iLog->Log(" Feature level: DirectX 10.1");
			break;
		case D3D_FEATURE_LEVEL_11_0:
			iLog->Log(" Feature level: DirectX 11.0");
			break;
		}
		iLog->Log(" Full stats: %s", m_strDeviceStats);
		if (m_devInfo.DriverType() == D3D_DRIVER_TYPE_HARDWARE)
			iLog->Log(" Rasterizer: Hardware");
		else if (m_devInfo.DriverType() == D3D_DRIVER_TYPE_REFERENCE)
			iLog->Log(" Rasterizer: Reference");
		else if (m_devInfo.DriverType() == D3D_DRIVER_TYPE_SOFTWARE)
			iLog->Log(" Rasterizer: Software");

#endif
		iLog->Log(" Current Resolution: %dx%dx%d %s", CRenderer::m_width, CRenderer::m_height, CRenderer::m_cbpp, m_bFullScreen ? "Full Screen" : "Windowed");
		iLog->Log(" HDR Rendering: %s", m_nHDRType == 1 ? "FP16" : m_nHDRType == 2 ? "MRT" : "Disabled");
		iLog->Log(" Occlusion queries: %s", (m_Features & RFT_OCCLUSIONTEST) ? "Supported" : "Not supported");
		iLog->Log(" Geometry instancing: %s", (m_bDeviceSupportsInstancing) ? "Supported" : "Not supported");
		iLog->Log(" NormalMaps compression : %s", m_hwTexFormatSupport.m_FormatBC5U.IsValid() ? "Supported" : "Not supported");
		iLog->Log(" Gamma control: %s", (m_Features & RFT_HWGAMMA) ? "Hardware" : "Software");
		iLog->Log(" Vertex Shaders version %d.%d", 4, 0);
		iLog->Log(" Pixel Shaders version %d.%d", 4, 0);

		CRenderer::OnChange_GeomInstancingThreshold(0);   // to get log printout and to set the internal value (vendor dependent)

		m_Features |= RFT_HW_SM20 | RFT_HW_SM2X | RFT_HW_SM30;

		if (!m_bDeviceSupportsInstancing)
			_SetVar("r_GeomInstancing", 0);

		const char* str = NULL;
		if (m_Features & RFT_HW_SM50)
			str = "SM.5.0";
		else if (m_Features & RFT_HW_SM40)
			str = "SM.4.0";
		else
			assert(0);
		iLog->Log(" Shader model usage: '%s'", str);

	}
	else
	{

		// force certain features during shader cache gen mode
		m_Features |= RFT_HW_SM20 | RFT_HW_SM2X | RFT_HW_SM30;

#if defined(ENABLE_NULL_D3D11DEVICE)
		m_DeviceWrapper.AssignDevice(new NullD3D11Device);
		D3DDeviceContext* pContext = NULL;
	#if defined(DEVICE_SUPPORTS_D3D11_3)
		GetDevice().GetImmediateContext3(&pContext);
	#elif defined(DEVICE_SUPPORTS_D3D11_2)
		GetDevice().GetImmediateContext2(&pContext);
	#elif defined(DEVICE_SUPPORTS_D3D11_1)
		GetDevice().GetImmediateContext1(&pContext);
	#else
		GetDevice().GetImmediateContext(&pContext);
	#endif
		m_DeviceContextWrapper.AssignDeviceContext(pContext);
#endif
	}

	iLog->Log(" *****************************************");
	iLog->Log(" ");

	iLog->Log("Init Shaders");

	//  if (!(GetFeatures() & (RFT_HW_PS2X | RFT_HW_PS30)))
	//    SetShaderQuality(eST_All, eSQ_Low);

	// Quality console variables --------------------------------------

#define QUALITY_VAR(name) { ICVar* pVar = iConsole->Register("q_Shader" # name, &m_cEF.m_ShaderProfiles[(int)eST_ ## name].m_iShaderProfileQuality, 1,          \
  0, CVARHELP("Defines the shader quality of " # name "\nUsage: q_Shader" # name " 0=low/1=med/2=high/3=very high (default)"), OnQShaderChange_Shader ## name); \
OnQShaderChange_Shader## name(pVar);                                                                                                                                                          \
iLog->Log(" %s shader quality: %s", # name, sGetSQuality("q_Shader" # name)); } // clamp for lowspec

	QUALITY_VAR(General);
	QUALITY_VAR(Metal);
	QUALITY_VAR(Glass);
	QUALITY_VAR(Vegetation);
	QUALITY_VAR(Ice);
	QUALITY_VAR(Terrain);
	QUALITY_VAR(Shadow);
	QUALITY_VAR(Water);
	QUALITY_VAR(FX);
	QUALITY_VAR(PostProcess);
	QUALITY_VAR(HDR);
	QUALITY_VAR(Sky);

#undef QUALITY_VAR

	ICVar* pVar = REGISTER_INT_CB("q_Renderer", 3, 0, "Defines the quality of Renderer\nUsage: q_Renderer 0=low/1=med/2=high/3=very high (default)", OnQShaderChange_Renderer);
	OnQShaderChange_Renderer(pVar);   // clamp for lowspec, report renderer current value
	iLog->Log("Render quality: %s", sGetSQuality("q_Renderer"));

	REGISTER_COMMAND("q_Quality", &Command_Quality, 0,
	                 "If called with a parameter it sets the quality of all q_.. variables\n"
	                 "otherwise it prints their current state\n"
	                 "Usage: q_Quality [0=low/1=med/2=high/3=very high]");

	REGISTER_COMMAND("r_ColorGradingChartImage", &Command_ColorGradingChartImage, 0,
	                 "If called with a parameter it loads a color chart image. This image will overwrite\n"
	                 " the dynamic color chart blending result and be used during post processing instead.\n"
	                 "If called with no parameter it displays the name of the previously loaded chart.\n"
	                 "To reset a previously loaded chart call r_ColorGradingChartImage 0.\n"
	                 "Usage: r_ColorGradingChartImage [path of color chart image/reset]");

#if defined(DURANGO_VSGD_CAP)
	REGISTER_COMMAND("GPUCapture", &GPUCapture, VF_NULL,
	                 "Usage: GPUCapture name"
	                 "Takes a PIX GPU capture with the specified name\n");
#endif

#if defined(OPENGL) && !DXGL_FULL_EMULATION
	#if CRY_OPENGL_SINGLE_CONTEXT
	if (!m_pRT->IsRenderThread())
		DXGLUnbindDeviceContext(GetDeviceContext().GetRealDeviceContext());
	#else
	if (!m_pRT->IsRenderThread())
		DXGLUnbindDeviceContext(GetDeviceContext().GetRealDeviceContext(), !CV_r_multithreaded);
	#endif
#endif //defined(OPENGL) && !DXGL_FULL_EMULATION

	if (!bShaderCacheGen)
		m_pRT->RC_Init();

	if (!g_shaderGeneralHeap)
		g_shaderGeneralHeap = CryGetIMemoryManager()->CreateGeneralExpandingMemoryHeap(4 * 1024 * 1024, 0, "Shader General");

	m_cEF.mfInit();

	if (!IsEditorMode() && !IsShaderCacheGenMode())
		m_pRT->RC_PrecacheDefaultShaders();

	//PostInit();

#if CRY_PLATFORM_WINDOWS
	// Initialize the set of connected monitors
	HandleMessage(0, WM_DEVICECHANGE, 0, 0, 0);
	m_bDisplayChanged = false;
#endif

#if defined(ENABLE_SIMPLE_GPU_TIMERS)
	if (m_pPipelineProfiler)
	{
		m_pPipelineProfiler->Init();
	}
#endif
	m_bInitialized = true;

	//  Cry_memcheck();

	// Success, return the window handle
	return (m_hWnd);
}

//=============================================================================

int CD3D9Renderer::EnumAAFormats(SAAFormat* formats)
{
#if defined(SUPPORT_DEVICE_INFO)

	int numFormats = 0;

	for (unsigned int i = 1; i <= D3D11_MAX_MULTISAMPLE_SAMPLE_COUNT; ++i)
	{
		unsigned int maxQuality;
		if (SUCCEEDED(m_devInfo.Device()->CheckMultisampleQualityLevels(m_devInfo.SwapChainDesc().BufferDesc.Format, i, &maxQuality)) && maxQuality > 0)
		{
			if (formats)
			{
				formats[numFormats].nSamples = i;
				formats[numFormats].nQuality = 0;
				formats[numFormats].szDescr[0] = 0;
			}

			++numFormats;
		}
	}

	return numFormats;

#else // #if defined(SUPPORT_DEVICE_INFO)

	return 0;

#endif
}

int CD3D9Renderer::GetAAFormat(TArray<SAAFormat>& Formats)
{
	int nNums = EnumAAFormats(NULL);
	if (nNums > 0)
	{
		Formats.resize(nNums);
		EnumAAFormats(&Formats[0]);
	}

	for (unsigned int i = 0; i < Formats.Num(); i++)
	{
		if (CV_r_msaa_samples == Formats[i].nSamples && CV_r_msaa_quality == Formats[i].nQuality)
			return (int) i;
	}

	return -1;
}

bool CD3D9Renderer::CheckMSAAChange()
{
	if (CV_r_msaa != m_MSAA)
	{
		iLog->LogError("MSAA is not supported any longer and will be removed in an upcoming version.");
		_SetVar("r_MSAA", 0);
		return false;
	}
	
	bool bChanged = false;
	if (!CV_r_HDRRendering && CV_r_msaa)
	{
		iLog->Log("MSAA in non-HDR mode is currently not supported (use \"r_HDRRendering 1\" or the options menu)");
		_SetVar("r_MSAA", 0);
	}
	if (CV_r_msaa != m_MSAA || (CV_r_msaa && (m_MSAA_quality != CV_r_msaa_quality || m_MSAA_samples != CV_r_msaa_samples)))
	{
		if (CV_r_msaa && (m_hwTexFormatSupport.m_FormatR16G16B16A16.bCanMultiSampleRT || m_hwTexFormatSupport.m_FormatR16G16.bCanMultiSampleRT))
		{
			CTexture::s_eTFZ = eTF_R32F;
			TArray<SAAFormat> Formats;
			int nNum = GetAAFormat(Formats);
			if (nNum < 0)
			{
				iLog->Log(" MSAA: Requested mode not supported\n");
				_SetVar("r_MSAA", 0);
				m_MSAA = 0;
			}
			else
			{
				iLog->Log(" MSAA: Enabled %d samples (quality level %d)", Formats[nNum].nSamples, Formats[nNum].nQuality);
				if (Formats[nNum].nQuality != m_MSAA_quality || Formats[nNum].nSamples != m_MSAA_samples)
				{
					bChanged = true;
					_SetVar("r_MSAA_quality", Formats[nNum].nQuality);
					_SetVar("r_MSAA_samples", Formats[nNum].nSamples);
				}
				else if (!m_MSAA)
					bChanged = true;
			}
		}
		else
		{
			CTexture::s_eTFZ = eTF_R32F;
			bChanged = true;
			iLog->Log(" MSAA: Disabled");
		}
		m_MSAA = CV_r_msaa;
		m_MSAA_quality = CV_r_msaa_quality;
		m_MSAA_samples = CV_r_msaa_samples;
	}

	return bChanged;
}

bool CD3D9Renderer::CheckSSAAChange()
{
	const int width = m_CVWidth ? m_CVWidth->GetIVal() : m_width;
	const int height = m_CVHeight ? m_CVHeight->GetIVal() : m_height;
	const int maxSamples = min(m_MaxTextureSize / width, m_MaxTextureSize / height);
	const int numSSAASamples = clamp_tpl(CV_r_Supersampling, 1, maxSamples);
	if (m_numSSAASamples != numSSAASamples)
	{
		m_numSSAASamples = numSSAASamples;
		return true;
	}
	return false;
}

//==========================================================================

void CD3D9Renderer::InitAMDAPI()
{
#if USE_AMD_API
	#if CRY_PLATFORM_WINDOWS
	do
	{
		AGSReturnCode status = AGSInit();
		iLog->Log("AGS: AMD GPU Services API init %s (%d)", status == AGS_SUCCESS ? "ok" : "failed", status);
		m_bVendorLibInitialized = status == AGS_SUCCESS;
		if (!m_bVendorLibInitialized)
			break;

		AGSDriverVersionInfoStruct driverInfo = { 0 };
		status = AGSDriverGetVersionInfo(&driverInfo);

		if (status != AGS_SUCCESS)
			iLog->LogError("AGS: Unable to get driver version (%d)", status);
		else
			iLog->Log("AGS: Catalyst Version: %s  Driver Version: %s", driverInfo.strCatalystVersion, driverInfo.strDriverVersion);

		int outputIndex = 0;
		#if defined(SUPPORT_DEVICE_INFO)
		outputIndex = (int)m_devInfo.OutputIndex();
		#else
		if (AGSGetDefaultDisplayIndex(&outputIndex) != AGS_SUCCESS)
			outputIndex = 0;
		#endif

		m_nGPUs = 1;
		int numGPUs = 1;
		status = AGSCrossfireGetGPUCount(outputIndex, &numGPUs);

		if (status != AGS_SUCCESS)
		{
			iLog->LogError("AGS: Unable to get crossfire info (%d)", status);
		}
		else
		{
			m_nGPUs = numGPUs;
			iLog->Log("AGS: Multi GPU count = %d", numGPUs);
		}
	}
	while (0);
	#endif // CRY_PLATFORM_WINDOWS

	#if defined(USE_AMD_EXT)
	do
	{
		PFNAmdDxExtCreate11 AmdDxExtCreate;
		HMODULE hDLL;
		HRESULT hr = S_OK;
		D3DDevice* device = NULL;
		device = GetDevice().GetRealDevice();

		#if defined _WIN64
		hDLL = GetModuleHandle("atidxx64.dll");
		#else
		hDLL = GetModuleHandle("atidxx32.dll");
		#endif

		g_pDepthBoundsTest = NULL;

		// Find the DLL entry point
		AmdDxExtCreate = reinterpret_cast<PFNAmdDxExtCreate11>(GetProcAddress(hDLL, "AmdDxExtCreate11"));
		if (AmdDxExtCreate == NULL)
		{
			g_bDepthBoundsTest = false;
			break;
		}

		// Create the extension object
		hr = AmdDxExtCreate(device, &g_pExtension);

		// Get the Extension Interfaces
		if (SUCCEEDED(hr))
		{
			g_pDepthBoundsTest = static_cast<IAmdDxExtDepthBounds*>(g_pExtension->GetExtInterface(AmdDxExtDepthBoundsID));
		}

		g_bDepthBoundsTest = g_pDepthBoundsTest != NULL;
	}
	while (0);

	#endif // CRY_PLATFORM_WINDOWS
#endif   // USE_AMD_API
}

void CD3D9Renderer::InitNVAPI()
{
#if defined(USE_NV_API)
	NvAPI_Status stat = NvAPI_Initialize();
	iLog->Log("NVAPI: API init %s (%d)", stat ? "failed" : "ok", stat);
	m_bVendorLibInitialized = stat == 0;

	if (!m_bVendorLibInitialized)
		return;

	NvU32 version;
	NvAPI_ShortString branch;
	NvAPI_Status status = NvAPI_SYS_GetDriverAndBranchVersion(&version, branch);
	if (status != NVAPI_OK)
		iLog->LogError("NVAPI: Unable to get driver version (%d)", status);

	// enumerate displays
	for (int i = 0; i < NVAPI_MAX_DISPLAYS; ++i)
	{
		NvDisplayHandle displayHandle;
		status = NvAPI_EnumNvidiaDisplayHandle(i, &displayHandle);
		if (status != NVAPI_OK)
			break;
	}

	m_nGPUs = 1;

	// check SLI state to get number of GPUs available for rendering
	NV_GET_CURRENT_SLI_STATE sliState;
	sliState.version = NV_GET_CURRENT_SLI_STATE_VER;
	D3DDevice* device = NULL;
	device = GetDevice().GetRealDevice();
	status = NvAPI_D3D_GetCurrentSLIState(device, &sliState);
	if (status != NVAPI_OK)
	{
		iLog->LogError("NVAPI: Unable to get SLI state (%d)", status);
	}
	else
	{
		m_nGPUs = sliState.numAFRGroups;
		if (m_nGPUs < 2)
		{
			iLog->Log("NVAPI: Single GPU system");
		}
		else
		{
			m_nGPUs = min((int)m_nGPUs, 31);
			iLog->Log("NVAPI: System configured as SLI: %d GPU(s) for rendering", m_nGPUs);
		}
	}

	m_bDeviceSupports_NVDBT = 1;
	iLog->Log("NVDBT supported");
#endif // USE_NV_API
}

bool CD3D9Renderer::SetRes()
{
	LOADING_TIME_PROFILE_SECTION;
	ChangeLog();

	m_pixelAspectRatio = 1.0f;
	m_dwCreateFlags = 0;

	///////////////////////////////////////////////////////////////////
#if CRY_PLATFORM_DURANGO
	HRESULT hr = S_OK;

	// On Durango we use an internal resolution of 720p but create a 1080p swap chain and do custom upscaling.
	// This is required since the swap chain scaling does just point filtering and since the engine requires the
	// backbuffer to be RGBA and not BGRA as required by the swap chain on Durango.

	// Create device
	uint32 creationFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;

	#if defined(SUPPORT_D3D_DEBUG_RUNTIME)
	creationFlags |= (CV_d3d11_debugruntime) ? D3D11_CREATE_DEVICE_DEBUG : 0; // Debug runtime
	#endif
	#ifdef USE_PIX_DURANGO
		#if defined(DURANGO_MONOD3D_DRIVER)
	creationFlags |= D3D11_CREATE_DEVICE_INSTRUMENTED;
		#else
	creationFlags |= D3D11_CREATE_DEVICE_PIX_PROFILING;
		#endif
	#endif

	creationFlags |= D3D11_CREATE_DEVICE_FAST_KICKOFFS; // AprilXDK QFE 4

	D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_0 };
	D3D_FEATURE_LEVEL actualFeatureLevel;

	{
		// create Device and DeviceContext
		ID3D11Device* pDevice = NULL;
		ID3D11DeviceContext* pDeviceContext = NULL;

		LOADING_TIME_PROFILE_SECTION_NAMED("CD3D9Renderer::SetRes(): D3D11CreateDevice()");
		hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, creationFlags, featureLevels, ARRAYSIZE(featureLevels), D3D11_SDK_VERSION, &pDevice, &actualFeatureLevel, &pDeviceContext);

		if (FAILED(hr))
			return false;

	#if defined(DEVICE_SUPPORTS_D3D11_1)
		// TODOL check documentation if it is ok to ignore the context from create device? (both pointers are the same), some refcounting to look out for?\	
		ID3D11Device1* pDevice1 = static_cast<ID3D11Device1*>(pDevice);
		m_DeviceWrapper.AssignDevice(pDevice1);

		ID3D11DeviceContext1* pDeviceContext1 = NULL;
		GetDevice().GetImmediateContext1(&pDeviceContext1);
		m_DeviceContextWrapper.AssignDeviceContext(pDeviceContext1);
	#else
		m_DeviceWrapper.AssignDevice(pDevice);
		m_DeviceContextWrapper.AssignDeviceContext(pDeviceContext);
	#endif
	}

	{
		// create performance context	and context
		ID3DXboxPerformanceDevice* pPerformanceDevice = NULL;
		ID3DXboxPerformanceContext* pDerformanceDEviceContext = NULL;

		hr = GetDevice().QueryInterface(__uuidof(ID3DXboxPerformanceDevice), (void**)&pPerformanceDevice);
		CHECK_HRESULT(hr);

		hr = GetDeviceContext().QueryInterface(__uuidof(ID3DXboxPerformanceContext), (void**)&pDerformanceDEviceContext);
		CHECK_HRESULT(hr);

		m_PerformanceDeviceWrapper.AssignPerformanceDevice(pPerformanceDevice);
		m_PerformanceDeviceContextWrapper.AssignPerformanceDeviceContext(pDerformanceDEviceContext);
	}

	hr = GetPerformanceDevice().SetDriverHint(XBOX_DRIVER_HINT_CONSTANT_BUFFER_OFFSETS_IN_CONSTANTS, 1);
	CHECK_HRESULT(hr);

	D3D11_DMA_ENGINE_CONTEXT_DESC dmaDesc = { 0 };
	dmaDesc.CreateFlags = D3D11_DMA_ENGINE_CONTEXT_CREATE_SDMA_1;
	dmaDesc.RingBufferSizeBytes = 0;

	GetPerformanceDevice().CreateDmaEngineContext(&dmaDesc, &m_pDMA1);

	IDXGIDevice1* dxgiDevice;
	hr |= GetDevice().QueryInterface(__uuidof(IDXGIDevice1), (void**)&dxgiDevice);

	PREFAST_ASSUME(dxgiDevice);
	IDXGIAdapter* dxgiAdapter;
	hr |= dxgiDevice->GetAdapter(&dxgiAdapter);

	IDXGIFactory2* dxgiFactory;
	hr |= dxgiAdapter->GetParent(__uuidof(IDXGIFactory2), (void**)&dxgiFactory);

	m_pPerformanceDeviceContext = NULL;
	hr |= GetDevice().QueryInterface(__uuidof(ID3DXboxPerformanceContext), (void**)&m_pPerformanceDeviceContext);

	// Create full HD swap chain with backbuffer
	DXGI_SWAP_CHAIN_DESC1 swapChainDesc = { 0 };
	swapChainDesc.Width = m_backbufferWidth;
	swapChainDesc.Height = m_backbufferHeight;
	swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.BufferCount = 2;
	swapChainDesc.Stereo = false;
	swapChainDesc.SampleDesc.Count = 1;
	swapChainDesc.SampleDesc.Quality = 0;
	swapChainDesc.Scaling = DXGI_SCALING_STRETCH;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
	swapChainDesc.Flags = 0;
	#if defined(DURANGO_MONOD3D_DRIVER)
	swapChainDesc.Flags |= DXGIX_SWAP_CHAIN_FLAG_QUANTIZATION_RGB_FULL;
	#endif
	m_backbufferWidth = swapChainDesc.Width;
	m_backbufferHeight = swapChainDesc.Height;
	{
		LOADING_TIME_PROFILE_SECTION_NAMED("CD3D9Renderer::SetRes(): CreateSwapChainForCoreWindow()");
		hr |= dxgiFactory->CreateSwapChainForCoreWindow(m_DeviceWrapper.GetRealDevice(), (IUnknown*)gEnv->pWindow, &swapChainDesc, nullptr, &m_pSwapChain);
	}
	if (FAILED(hr)) return false;

	dxgiDevice->SetMaximumFrameLatency(MAX_FRAME_LATENCY);

	D3DTexture* pBackBuffer;
	{
		D3DSurface* pBuffer;
		LOADING_TIME_PROFILE_SECTION_NAMED("CD3D9Renderer::SetRes(): backbuffer");
		hr |= m_pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&pBackBuffer);
		hr |= GetDevice().CreateRenderTargetView(pBackBuffer, NULL, &pBuffer);

		m_pBackBuffer = pBuffer;
	}

	m_pBackBuffers.push_back(m_pBackBuffer); // For backbuffer swapping logic for DX11, 1 item in the collection will work as the "current index" is always 0 in non-DX12
	SAFE_RELEASE(pBackBuffer);

	if (FAILED(hr)) return false;

	#ifdef USE_PIX_DURANGO
	HRESULT res = GetDeviceContext().QueryInterface(__uuidof(ID3DUserDefinedAnnotation), reinterpret_cast<void**>(&m_pPixPerf));
	assert(SUCCEEDED(res));
	#endif

	float colBlack[4] = { 0 };
	GetDeviceContext().ClearRenderTargetView(m_pBackBuffer, colBlack);

	// Post device creation callbacks
	OnD3D11CreateDevice(GetDevice().GetRealDevice());

	OnD3D11PostCreateDevice(GetDevice().GetRealDevice());

#elif CRY_PLATFORM_MOBILE

	m_bFullScreen = true;

	if (!m_devInfo.CreateDevice(false, m_width, m_height, m_backbufferWidth, m_backbufferHeight, m_zbpp, OnD3D11CreateDevice, CreateWindowCallback))
		return false;
	m_devInfo.SyncInterval() = m_VSync ? 1 : 0;

	OnD3D11PostCreateDevice(m_devInfo.Device());

	AdjustWindowForChange();

#elif CRY_PLATFORM_WINDOWS || CRY_PLATFORM_APPLE || CRY_PLATFORM_LINUX || CRY_PLATFORM_ANDROID

	UnSetRes();

	int width = m_width;
	int height = m_height;
	if (IsEditorMode())
	{
		// Note: Editor is a special case, m_backbufferWidth needs to be the same as m_width
		width = m_deskwidth;
		height = m_deskheight;
	}

	// DirectX9 and DirectX10 device creating
	#if defined(SUPPORT_DEVICE_INFO)
	if (m_devInfo.CreateDevice(!m_bFullScreen, width, height, m_backbufferWidth, m_backbufferHeight, m_zbpp, OnD3D11CreateDevice, CreateWindowCallback))
	{
		m_devInfo.SyncInterval() = m_VSync ? 1 : 0;
	}
	else
	{
		return false;
	}

	OnD3D11PostCreateDevice(m_devInfo.Device());
	#endif

	AdjustWindowForChange();

#elif CRY_PLATFORM_ORBIS

	SCryVideoMode VideoMode;
	DXOrbis::CreateCCryDXOrbisRenderDevice();
	DXOrbis::CreateCCryDXOrbisSwapChain();
	DXOrbis::Device()->RegisterDeviceThread();

	VideoMode.ResolutionID = 0;
	VideoMode.dwDisplayWidth = 1920;
	VideoMode.dwDisplayHeight = 1080;
	VideoMode.fIsWideScreen = true;
	VideoMode.fIsInterlaced = false;
	VideoMode.fIsHiDef = true;
	VideoMode.RefreshRate = 60.0f;
	SCryPresentParams d3dpp;
	if (FAILED(SetupPresentationParameters(m_pixelAspectRatio, d3dpp, VideoMode)))
	{
		iLog->LogError("Error: Could not set up presentation parameters.");
		return false;
	}

	DXGI_SWAP_CHAIN_DESC scDesc;
	scDesc.BufferDesc.Width = m_backbufferWidth;
	scDesc.BufferDesc.Height = m_backbufferHeight;
	scDesc.BufferDesc.RefreshRate.Numerator = 0;
	scDesc.BufferDesc.RefreshRate.Denominator = 1;
	scDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	scDesc.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
	scDesc.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;

	scDesc.SampleDesc.Count = 1;
	scDesc.SampleDesc.Quality = 0;

	scDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	scDesc.BufferCount = 1;
	//scDesc.OutputWindow = (typeof(scDesc.OutputWindow))m_CurrContext->m_hWnd;
	scDesc.Windowed = TRUE;
	scDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
	scDesc.Flags = 0;
	D3DDevice* pd3dDevice = NULL;
	D3DDeviceContext* pd3dDeviceContext = NULL;
	HRESULT hr = D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, NULL, 0, 0, &scDesc, &m_pSwapChain, &pd3dDevice, NULL, &pd3dDeviceContext);
	GetDevice().AssignDevice(pd3dDevice);
	GetDeviceContext().AssignDeviceContext(pd3dDeviceContext);

	D3DTexture* pBackBuffer = nullptr;
	D3DSurface* pBackBufferRTV = nullptr;
	hr |= m_pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&pBackBuffer);
	hr |= GetDevice().CreateRenderTargetView(pBackBuffer, NULL, &pBackBufferRTV);
	m_pBackBuffer = pBackBufferRTV;
	m_pBackBuffers.push_back(m_pBackBuffer);
	SAFE_RELEASE(pBackBuffer);
	SAFE_RELEASE(pBackBufferRTV);

	OnD3D11CreateDevice(GetDevice().GetRealDevice());
	OnD3D11PostCreateDevice(GetDevice().GetRealDevice());

#else
	#error UNKNOWN RENDER DEVICE PLATFORM
#endif

	for (uint32 id = 0; id < RT_COMMAND_BUF_COUNT; ++id)
	{
		m_RP.m_TI[id].m_matView = new CMatrixStack(16, 0);
		if (m_RP.m_TI[id].m_matView == NULL)
			return false;
		m_RP.m_TI[id].m_matProj = new CMatrixStack(16, 0);
		if (m_RP.m_TI[id].m_matProj == NULL)
			return false;
		m_RP.m_TI[id].m_matCameraZero.SetIdentity();
	}

	m_DevBufMan.Init();

	m_pStereoRenderer->InitDeviceAfterD3D();

	return true;
}

bool SPixFormat::CheckSupport(D3DFormat Format, const char* szDescr, ETexture_Usage eTxUsage)
{
	bool bRes = true;
	CD3D9Renderer* rd = gcpRendD3D;

	UINT nOptions;
	HRESULT hr = gcpRendD3D->GetDevice().CheckFormatSupport(Format, &nOptions);
	if (SUCCEEDED(hr))
	{
		if (nOptions & (D3D11_FORMAT_SUPPORT_TEXTURE2D | D3D11_FORMAT_SUPPORT_TEXTURECUBE))
		{
			bool canAutoGenMips = (nOptions & D3D11_FORMAT_SUPPORT_MIP_AUTOGEN) != 0;
			bool canReadSRGB = CTexture::IsDeviceFormatSRGBReadable(Format);

			Init();
			DeviceFormat = Format;
#if !CRY_PLATFORM_ORBIS
			MaxWidth = D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION;
			MaxHeight = D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION;
#else
			MaxWidth = 2048;
			MaxHeight = 2048;
#endif
			Desc = szDescr;
			BitsPerPixel = CTexture::BitsPerPixel(CTexture::TexFormatFromDeviceFormat(Format));

			bCanDS = (nOptions & D3D11_FORMAT_SUPPORT_DEPTH_STENCIL) != 0;
			bCanRT = (nOptions & D3D11_FORMAT_SUPPORT_RENDER_TARGET) != 0;
			bCanMultiSampleRT = (nOptions & D3D11_FORMAT_SUPPORT_MULTISAMPLE_RENDERTARGET) != 0;
			bool bCanMips = (nOptions & D3D11_FORMAT_SUPPORT_MIP) != 0;
			bCanMipsAutoGen = (nOptions & D3D11_FORMAT_SUPPORT_MIP_AUTOGEN) != 0;
			bCanGather = (nOptions & D3D11_FORMAT_SUPPORT_SHADER_GATHER) != 0;
			bCanGatherCmp = (nOptions & D3D11_FORMAT_SUPPORT_SHADER_GATHER_COMPARISON) != 0;
			bCanBlend = (nOptions & D3D11_FORMAT_SUPPORT_BLENDABLE) != 0;
			bCanReadSRGB = canReadSRGB;

			if (bCanDS || bCanRT || bCanGather || bCanBlend || bCanReadSRGB || bCanMips)
				iLog->Log("  %s%s%s%s%s%s%s%s%s%s",
				          szDescr,
				          bCanMips ? ", mips" : "",
				          bCanMipsAutoGen ? " (autogen)" : "",
				          bCanReadSRGB ? ", sRGB" : "",
				          bCanBlend ? ", blend" : "",
				          bCanDS ? ", DS" : "",
				          bCanRT ? ", RT" : "",
				          bCanMultiSampleRT ? " (multi-sampled)" : "",
				          bCanGather ? ", gather" : "",
				          bCanGatherCmp ? " (comparable)" : ""
				          );
			else
				iLog->Log("  %s", szDescr);

			Next = rd->m_hwTexFormatSupport.m_FirstPixelFormat;
			rd->m_hwTexFormatSupport.m_FirstPixelFormat = this;
		}
		else
			bRes = false;
	}
	else
		bRes = false;

	return bRes;
}

void SPixFormatSupport::CheckFormatSupport()
{
	iLog->Log("Using pixel texture formats:");

	m_FirstPixelFormat = NULL;

	m_FormatR8G8B8A8S.CheckSupport(DXGI_FORMAT_R8G8B8A8_SNORM, "R8G8B8A8S");
	m_FormatR8G8B8A8.CheckSupport(DXGI_FORMAT_R8G8B8A8_UNORM, "R8G8B8A8");

	m_FormatR1.CheckSupport(DXGI_FORMAT_R1_UNORM, "R1");
	m_FormatA8.CheckSupport(DXGI_FORMAT_A8_UNORM, "A8");
	m_FormatR8.CheckSupport(DXGI_FORMAT_R8_UNORM, "R8");
	m_FormatR8S.CheckSupport(DXGI_FORMAT_R8_SNORM, "R8S");
	m_FormatR16.CheckSupport(DXGI_FORMAT_R16_UNORM, "R16");
	m_FormatR16F.CheckSupport(DXGI_FORMAT_R16_FLOAT, "R16F");
	m_FormatR32F.CheckSupport(DXGI_FORMAT_R32_FLOAT, "R32F");
	m_FormatR8G8.CheckSupport(DXGI_FORMAT_R8G8_UNORM, "R8G8");
	m_FormatR8G8S.CheckSupport(DXGI_FORMAT_R8G8_SNORM, "R8G8S");
	m_FormatR16G16.CheckSupport(DXGI_FORMAT_R16G16_UNORM, "R16G16");
	m_FormatR16G16S.CheckSupport(DXGI_FORMAT_R16G16_SNORM, "R16G16S");
	m_FormatR16G16F.CheckSupport(DXGI_FORMAT_R16G16_FLOAT, "R16G16F");
	m_FormatR11G11B10F.CheckSupport(DXGI_FORMAT_R11G11B10_FLOAT, "R11G11B10F");
	m_FormatR10G10B10A2.CheckSupport(DXGI_FORMAT_R10G10B10A2_UNORM, "R10G10B10A2");
	m_FormatR16G16B16A16.CheckSupport(DXGI_FORMAT_R16G16B16A16_UNORM, "R16G16B16A16");
	m_FormatR16G16B16A16S.CheckSupport(DXGI_FORMAT_R16G16B16A16_SNORM, "R16G16B16A16S");
	m_FormatR16G16B16A16F.CheckSupport(DXGI_FORMAT_R16G16B16A16_FLOAT, "R16G16B16A16F");
	m_FormatR32G32B32A32F.CheckSupport(DXGI_FORMAT_R32G32B32A32_FLOAT, "R32G32B32A32F");

	m_FormatBC1.CheckSupport(DXGI_FORMAT_BC1_UNORM, "BC1");
	m_FormatBC2.CheckSupport(DXGI_FORMAT_BC2_UNORM, "BC2");
	m_FormatBC3.CheckSupport(DXGI_FORMAT_BC3_UNORM, "BC3");
	m_FormatBC4U.CheckSupport(DXGI_FORMAT_BC4_UNORM, "BC4");
	m_FormatBC4S.CheckSupport(DXGI_FORMAT_BC4_SNORM, "BC4S");
	m_FormatBC5U.CheckSupport(DXGI_FORMAT_BC5_UNORM, "BC5");
	m_FormatBC5S.CheckSupport(DXGI_FORMAT_BC5_SNORM, "BC5S");
	m_FormatBC6UH.CheckSupport(DXGI_FORMAT_BC6H_UF16, "BC6UH");
	m_FormatBC6SH.CheckSupport(DXGI_FORMAT_BC6H_SF16, "BC6SH");
	m_FormatBC7.CheckSupport(DXGI_FORMAT_BC7_UNORM, "BC7");
	m_FormatR9G9B9E5.CheckSupport(DXGI_FORMAT_R9G9B9E5_SHAREDEXP, "R9G9B9E5");

	// Depth formats
	m_FormatD32FS8.CheckSupport(DXGI_FORMAT_R32G8X24_TYPELESS, "R32FX8T");
	m_FormatD32F.CheckSupport(DXGI_FORMAT_R32_TYPELESS, "R32T");
	m_FormatD24S8.CheckSupport(DXGI_FORMAT_R24G8_TYPELESS, "R24G8T");
	m_FormatD16.CheckSupport(DXGI_FORMAT_R16_TYPELESS, "R16T");

	m_FormatB5G6R5.CheckSupport(DXGI_FORMAT_B5G6R5_UNORM, "B5G6R5");
	m_FormatB5G5R5.CheckSupport(DXGI_FORMAT_B5G5R5A1_UNORM, "B5G5R5");
	//	m_FormatB4G4R4A4.CheckSupport(DXGI_FORMAT_B4G4R4A4_UNORM, "B4G4R4A4");

	m_FormatB8G8R8A8.CheckSupport(DXGI_FORMAT_B8G8R8A8_UNORM, "B8G8R8A8");
	m_FormatB8G8R8X8.CheckSupport(DXGI_FORMAT_B8G8R8X8_UNORM, "B8G8R8X8");

#if defined(OPENGL)
	m_FormatEAC_R11.CheckSupport(DXGI_FORMAT_EAC_R11_UNORM, "EAC_R11");
	m_FormatEAC_RG11.CheckSupport(DXGI_FORMAT_EAC_RG11_UNORM, "EAC_RG11");
	m_FormatETC2.CheckSupport(DXGI_FORMAT_ETC2_UNORM, "ETC2");
	m_FormatETC2A.CheckSupport(DXGI_FORMAT_ETC2A_UNORM, "ETC2A");
#endif //defined(OPENGL)
}

void CD3D9Renderer::GetVideoMemoryUsageStats(size_t& vidMemUsedThisFrame, size_t& vidMemUsedRecently, bool bGetPoolsSizes)
{

	if (bGetPoolsSizes)
	{
		vidMemUsedThisFrame = vidMemUsedRecently = (GetTexturesStreamPoolSize() + CV_r_rendertargetpoolsize) * 1024 * 1024;
	}
	else
	{
#if CRY_USE_DX12
		CD3D9Renderer* rd = gcpRendD3D;
		DXGI_QUERY_VIDEO_MEMORY_INFO videoMemoryInfoA;
		DXGI_QUERY_VIDEO_MEMORY_INFO videoMemoryInfoB;

		rd->m_devInfo.Adapter()->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &videoMemoryInfoA);
		rd->m_devInfo.Adapter()->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL, &videoMemoryInfoB);

		vidMemUsedThisFrame = size_t(videoMemoryInfoA.CurrentUsage);
		vidMemUsedRecently = 0;
#else
		assert("CD3D9Renderer::GetVideoMemoryUsageStats() not implemented for this platform yet!");
		vidMemUsedThisFrame = vidMemUsedRecently = 0;
#endif
	}
}

//===========================================================================================

HRESULT CALLBACK CD3D9Renderer::OnD3D11CreateDevice(D3DDevice* pd3dDevice)
{
	LOADING_TIME_PROFILE_SECTION;
	CD3D9Renderer* rd = gcpRendD3D;
	rd->m_DeviceWrapper.AssignDevice(pd3dDevice);

#if defined(SUPPORT_DEVICE_INFO)
	rd->m_DeviceContextWrapper.AssignDeviceContext(rd->m_devInfo.Context());
#endif
	rd->m_Features |= RFT_OCCLUSIONQUERY | RFT_ALLOWANISOTROPIC | RFT_HW_SM20 | RFT_HW_SM2X | RFT_HW_SM30 | RFT_HW_SM40 | RFT_HW_SM50;

#if defined(SUPPORT_D3D_DEBUG_RUNTIME)
	rd->m_d3dDebug.Init(pd3dDevice);
	rd->m_d3dDebug.Update(ESeverityCombination(CV_d3d11_debugMuteSeverity->GetIVal()), CV_d3d11_debugMuteMsgID->GetString(), CV_d3d11_debugBreakOnMsgID->GetString());
	rd->m_bUpdateD3DDebug = false;
#endif

#if defined(SUPPORT_DEVICE_INFO)
	rd->BindContextToThread(CryGetCurrentThreadId());

	LARGE_INTEGER driverVersion;
	driverVersion.LowPart = 0;
	driverVersion.HighPart = 0;
	rd->m_devInfo.Adapter()->CheckInterfaceSupport(__uuidof(ID3D10Device), &driverVersion);
	iLog->Log("D3D Adapter: Description: %ls", rd->m_devInfo.AdapterDesc().Description);
	iLog->Log("D3D Adapter: Driver version (UMD): %d.%02d.%02d.%04d", HIWORD(driverVersion.u.HighPart), LOWORD(driverVersion.u.HighPart), HIWORD(driverVersion.u.LowPart), LOWORD(driverVersion.u.LowPart));
	iLog->Log("D3D Adapter: VendorId = 0x%.4X", rd->m_devInfo.AdapterDesc().VendorId);
	iLog->Log("D3D Adapter: DeviceId = 0x%.4X", rd->m_devInfo.AdapterDesc().DeviceId);
	iLog->Log("D3D Adapter: SubSysId = 0x%.8X", rd->m_devInfo.AdapterDesc().SubSysId);
	iLog->Log("D3D Adapter: Revision = %i", rd->m_devInfo.AdapterDesc().Revision);

	// Vendor-specific initializations and workarounds for driver bugs.
	{
		if (rd->m_devInfo.AdapterDesc().VendorId == 4098)
		{
			rd->m_Features |= RFT_HW_ATI;
			rd->InitAMDAPI();
			iLog->Log("D3D Detected: AMD video card");
		}
		else if (rd->m_devInfo.AdapterDesc().VendorId == 4318)
		{
			rd->m_Features |= RFT_HW_NVIDIA;
			rd->InitNVAPI();
			iLog->Log("D3D Detected: NVIDIA video card");
		}
		else if (rd->m_devInfo.AdapterDesc().VendorId == 8086)
		{
			rd->m_Features |= RFT_HW_INTEL;
			iLog->Log("D3D Detected: intel video card");
		}
	}

	rd->m_nGPUs = min(rd->m_nGPUs, (uint32)MAX_GPU_NUM);
#endif

	CryLogAlways("Active GPUs: %i", rd->m_nGPUs);

#if CRY_PLATFORM_ORBIS
	rd->m_NumResourceSlots = 128;
	rd->m_NumSamplerSlots = 16;
	rd->m_MaxAnisotropyLevel = min(16, CRenderer::CV_r_texmaxanisotropy);
#else
	rd->m_NumResourceSlots = D3D11_COMMONSHADER_INPUT_RESOURCE_REGISTER_COUNT;
	rd->m_NumSamplerSlots = D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT;
	rd->m_MaxAnisotropyLevel = min(D3D11_REQ_MAXANISOTROPY, CRenderer::CV_r_texmaxanisotropy);
#endif

#if CRY_PLATFORM_WINDOWS
	HWND hWndDesktop = GetDesktopWindow();
	HDC dc = GetDC(hWndDesktop);
	uint16 gamma[3][256];
	if (GetDeviceGammaRamp(dc, gamma))
		rd->m_Features |= RFT_HWGAMMA;
	ReleaseDC(hWndDesktop, dc);
#endif

	// For safety, lots of drivers don't handle tiny texture sizes too tell.
#if defined(SUPPORT_DEVICE_INFO)
	rd->m_MaxTextureMemory = rd->m_devInfo.AdapterDesc().DedicatedVideoMemory;
#else
	rd->m_MaxTextureMemory = 256 * 1024 * 1024;

	#if CRY_PLATFORM_DURANGO || CRY_PLATFORM_ORBIS
	rd->m_MaxTextureMemory = 1024 * 1024 * 1024;
	#endif
#endif
	if (CRenderer::CV_r_TexturesStreamPoolSize <= 0)
		CRenderer::CV_r_TexturesStreamPoolSize = (int)(rd->m_MaxTextureMemory / 1024.0f / 1024.0f * 0.75f);

#if CRY_PLATFORM_ORBIS
	rd->m_MaxTextureSize = 8192;
	rd->m_bDeviceSupportsInstancing = true;
#else
	rd->m_MaxTextureSize = D3D11_REQ_FILTERING_HW_ADDRESSABLE_RESOURCE_DIMENSION;
	rd->m_bDeviceSupportsInstancing = true;
#endif

#if !defined(OPENGL_ES)
	rd->m_bDeviceSupportsGeometryShaders = (rd->m_Features & RFT_HW_SM40) != 0;
#else
	rd->m_bDeviceSupportsGeometryShaders = false;
#endif

#if !defined(OPENGL)
	rd->m_bDeviceSupportsTessellation = (rd->m_Features & RFT_HW_SM50) != 0;
#else
	rd->m_bDeviceSupportsTessellation = false;
#endif

	rd->m_Features |= RFT_OCCLUSIONTEST;

	rd->m_bUseWaterTessHW = CV_r_WaterTessellationHW != 0 && rd->m_bDeviceSupportsTessellation;

	PREFAST_SUPPRESS_WARNING(6326); //not a constant vs constant comparison on Win32/Win64
	rd->m_bUseSilhouettePOM = CV_r_SilhouettePOM != 0;
	CV_r_DeferredShadingAmbientSClear = !(rd->m_Features & RFT_HW_NVIDIA) ? 0 : CV_r_DeferredShadingAmbientSClear;

	// Handle the texture formats we need.
	{
		// Find the needed texture formats.
		rd->m_hwTexFormatSupport.CheckFormatSupport();

		rd->m_HDR_FloatFormat_Scalar = eTF_R32F;
		rd->m_HDR_FloatFormat = eTF_R16G16B16A16F;

		if (rd->m_hwTexFormatSupport.m_FormatBC1.IsValid() || rd->m_hwTexFormatSupport.m_FormatBC2.IsValid() || rd->m_hwTexFormatSupport.m_FormatBC3.IsValid())
			rd->m_Features |= RFT_COMPRESSTEXTURE;
	}

	rd->m_Features |= RFT_HW_HDR;

	iLog->Log(" Renderer HDR_Scalar: %s", CTexture::NameForTextureFormat(rd->m_HDR_FloatFormat_Scalar));

	rd->m_nHDRType = 1;

	rd->m_FullResRect.right = rd->m_width;
	rd->m_FullResRect.bottom = rd->m_height;

#if CRY_PLATFORM_WINDOWS || CRY_PLATFORM_APPLE || CRY_PLATFORM_LINUX || CRY_PLATFORM_ANDROID
	rd->m_pRT->RC_SetViewport(0, 0, rd->m_width, rd->m_height);
#else
	rd->RT_SetViewport(0, 0, rd->m_width, rd->m_height);
#endif
	rd->m_MainViewport.nX = 0;
	rd->m_MainViewport.nY = 0;
	rd->m_MainViewport.nWidth = rd->m_width;
	rd->m_MainViewport.nHeight = rd->m_height;

	return S_OK;
}

HRESULT CALLBACK CD3D9Renderer::OnD3D11PostCreateDevice(D3DDevice* pd3dDevice)
{
	LOADING_TIME_PROFILE_SECTION;
	CD3D9Renderer* rd = gcpRendD3D;
	HRESULT hr;

#if CAPTURE_REPLAY_LOG
	rd->MemReplayWrapD3DDevice();
#endif

#if defined(SUPPORT_DEVICE_INFO)
	rd->BindContextToThread(CryGetCurrentThreadId());
	rd->m_DeviceContextWrapper.AssignDeviceContext(rd->m_devInfo.Context());

	rd->m_pSwapChain = rd->m_devInfo.SwapChain();
	rd->m_pBackBuffer = rd->m_devInfo.BackbufferRTV();
	rd->m_pBackBuffers = rd->m_devInfo.BackbufferRTVs();
	rd->m_pCurrentBackBufferIndex = rd->m_devInfo.GetCurrentBackBufferIndex();
#endif

	DXGI_SWAP_CHAIN_DESC backBufferSurfaceDesc;
	hr = rd->m_pSwapChain->GetDesc(&backBufferSurfaceDesc);

	ZeroMemory(&rd->m_d3dsdBackBuffer, sizeof(DXGI_SURFACE_DESC));
	rd->m_d3dsdBackBuffer.Width = (UINT) backBufferSurfaceDesc.BufferDesc.Width;
	rd->m_d3dsdBackBuffer.Height = (UINT) backBufferSurfaceDesc.BufferDesc.Height;
#if defined(SUPPORT_DEVICE_INFO)
	rd->m_d3dsdBackBuffer.Format = backBufferSurfaceDesc.BufferDesc.Format;
	rd->m_d3dsdBackBuffer.SampleDesc = backBufferSurfaceDesc.SampleDesc;
	rd->m_ZFormat = rd->m_devInfo.AutoDepthStencilFmt();
#elif CRY_PLATFORM_ORBIS
	rd->m_d3dsdBackBuffer.Format = backBufferSurfaceDesc.BufferDesc.Format;
	rd->m_d3dsdBackBuffer.SampleDesc = backBufferSurfaceDesc.SampleDesc;
	rd->m_ZFormat = rd->m_zbpp == 32 ? DXGI_FORMAT_D32_FLOAT_S8X24_UINT : DXGI_FORMAT_D24_UNORM_S8_UINT;
#elif CRY_PLATFORM_DURANGO
	rd->m_d3dsdBackBuffer.Format = DXGI_FORMAT_R8G8B8A8_TYPELESS;
	rd->m_d3dsdBackBuffer.SampleDesc.Count = 1;
	rd->m_d3dsdBackBuffer.SampleDesc.Quality = 0;
	rd->m_ZFormat = rd->m_zbpp == 32 ? DXGI_FORMAT_R32G8X24_TYPELESS : DXGI_FORMAT_R24G8_TYPELESS;
#endif

	if (FAILED(hr))
		return hr;

	// Prepare backbuffer texture
	if (!rd->m_pBackBufferTexture)
	{
		ETEX_Format format = CTexture::TexFormatFromDeviceFormat(rd->m_d3dsdBackBuffer.Format);
		rd->m_pBackBufferTexture = CTexture::CreateTextureObject("$SwapChainBackBuffer", 0, 0, 1, eTT_2D, FT_DONT_STREAM | FT_USAGE_RENDERTARGET, format);
	}
	if (!gEnv->IsEditor())
	{
#if CRY_PLATFORM_ORBIS
		CCryDXOrbisTexture* pBackbuffer;
		DXOrbis::m_pSwapChain->GetBuffer(0, ID3D11Texture2D__GUID, (void**)&pBackbuffer);
		CDeviceTexture* const pDeviceTexture = new CDeviceTexture(pBackbuffer);
#elif CRY_PLATFORM_DURANGO
		D3DBaseTexture* pBackBuffer;
		HRESULT hr = rd->m_pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&pBackBuffer);
		CDeviceTexture* const pDeviceTexture = new CDeviceTexture(pBackBuffer);
#elif defined(SUPPORT_DEVICE_INFO)
		CDeviceTexture* pDeviceTexture = new CDeviceTexture(rd->m_devInfo.BackbufferTex2D());
#else
		assert(0);
#endif
		rd->m_pBackBufferTexture->SetDevTexture(pDeviceTexture);
		rd->m_pBackBufferTexture->SetWidth(rd->m_d3dsdBackBuffer.Width);
		rd->m_pBackBufferTexture->SetHeight(rd->m_d3dsdBackBuffer.Height);
		rd->m_pBackBufferTexture->ClosestFormatSupported(rd->m_pBackBufferTexture->GetDstFormat());
	}

	const float clearDepth = CRenderer::CV_r_ReverseDepth ? 0.f : 1.f;
	const uint clearStencil = 1;
	const ColorF clearValues = ColorF(clearDepth, FLOAT(clearStencil), 0.f, 0.f);

	int nDepthBufferWidth = rd->IsEditorMode() ? rd->m_d3dsdBackBuffer.Width : rd->GetOverlayWidth();
	int nDepthBufferHeight = rd->IsEditorMode() ? rd->m_d3dsdBackBuffer.Height : rd->GetOverlayHeight();

	rd->m_pZTexture = CTexture::CreateRenderTarget("$DeviceDepthScene", nDepthBufferWidth, nDepthBufferHeight,
	                                               clearValues, eTT_2D, FT_USAGE_DEPTHSTENCIL | FT_DONT_RELEASE | FT_DONT_STREAM, rd->m_zbpp == 32 ? eTF_D32FS8 : eTF_D24S8);
#if defined(DURANGO_USE_ESRAM)
	rd->m_pZTexture->SetESRAMOffset(11894784 + 5955584 * 2);
#endif

	D3DTexture* pZTarget = rd->m_pZTexture->GetDevTexture()->Get2DTexture();
	D3DDepthSurface* pZSurface = rd->m_pZTexture->GetDeviceDepthStencilView(0, -1, rd->m_d3dsdBackBuffer.SampleDesc.Count > 1, false);
	rd->m_pZTexture->SetShaderResourceView(rd->m_pZTexture->GetDeviceDepthReadOnlySRV(0, -1, rd->m_d3dsdBackBuffer.SampleDesc.Count > 1), rd->m_d3dsdBackBuffer.SampleDesc.Count > 1);

	rd->GetDeviceContext().ClearDepthStencilView(pZSurface, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, clearDepth, clearStencil);

	rd->m_DepthBufferOrig.pTexture = rd->m_pZTexture;
	rd->m_DepthBufferOrig.pTarget = pZTarget;
	rd->m_DepthBufferOrig.pSurface = pZSurface;
	rd->m_DepthBufferOrig.nWidth = nDepthBufferWidth;
	rd->m_DepthBufferOrig.nHeight = nDepthBufferHeight;
	rd->m_DepthBufferOrig.bBusy = true;
	rd->m_DepthBufferOrig.nFrameAccess = -2;

	rd->m_DepthBufferOrigMSAA = rd->m_DepthBufferOrig;
	rd->m_DepthBufferNative= rd->m_DepthBufferOrig;

	// Create the native resolution depth stencil buffer for overlay rendering if needed
	if (!rd->IsEditorMode() && (gcpRendD3D->GetOverlayWidth() != nDepthBufferWidth || gcpRendD3D->GetOverlayHeight() != nDepthBufferHeight))
	{
		rd->m_pNativeZTexture = CTexture::CreateRenderTarget("$DeviceDepthOverlay", rd->GetOverlayWidth(), rd->GetOverlayHeight(),
		                                                     clearValues, eTT_2D, FT_USAGE_DEPTHSTENCIL | FT_DONT_RELEASE | FT_DONT_STREAM, rd->m_zbpp == 32 ? eTF_D32FS8 : eTF_D24S8);
		
		D3DTexture* pNativeZTarget = rd->m_pZTexture->GetDevTexture()->Get2DTexture();
		D3DDepthSurface* pNativeZSurface = rd->m_pZTexture->GetDeviceDepthStencilView(0, -1, rd->m_d3dsdBackBuffer.SampleDesc.Count > 1, false);
		rd->m_pNativeZTexture->SetShaderResourceView(rd->m_pNativeZTexture->GetDeviceDepthReadOnlySRV(0, -1, rd->m_d3dsdBackBuffer.SampleDesc.Count > 1), rd->m_d3dsdBackBuffer.SampleDesc.Count > 1);

		rd->GetDeviceContext().ClearDepthStencilView(pNativeZSurface, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, clearDepth, clearStencil);

	rd->m_DepthBufferNative.pTexture = rd->m_pNativeZTexture;
		rd->m_DepthBufferNative.pTarget = pNativeZTarget;
		rd->m_DepthBufferNative.pSurface = pNativeZSurface;
	rd->m_DepthBufferNative.nWidth = rd->m_nativeWidth;
	rd->m_DepthBufferNative.nHeight = rd->m_nativeHeight;
	}

	rd->m_nRTStackLevel[0] = 0;
	if (rd->m_d3dsdBackBuffer.Width == rd->m_nativeWidth && rd->m_d3dsdBackBuffer.Height == rd->m_nativeHeight)
	{
		rd->m_RTStack[0][0].m_pDepth = rd->m_DepthBufferNative.pSurface;
		rd->m_RTStack[0][0].m_pSurfDepth = &rd->m_DepthBufferNative;
	}
	else
	{
		rd->m_RTStack[0][0].m_pDepth = NULL;
		rd->m_RTStack[0][0].m_pSurfDepth = NULL;
	}
	rd->m_RTStack[0][0].m_pTarget = rd->m_pBackBuffer;
	rd->m_RTStack[0][0].m_Width = rd->m_d3dsdBackBuffer.Width;
	rd->m_RTStack[0][0].m_Height = rd->m_d3dsdBackBuffer.Height;
#if CRY_PLATFORM_DURANGO
	rd->m_RTStack[0][0].m_bScreenVP = true;
#else
	rd->m_RTStack[0][0].m_bScreenVP = false;
#endif
	rd->m_RTStack[0][0].m_bWasSetRT = false;
	rd->m_RTStack[0][0].m_bWasSetD = false;
	rd->m_nMaxRT2Commit = 0;
	rd->m_pNewTarget[0] = &rd->m_RTStack[0][0];
	rd->FX_SetActiveRenderTargets();

	for (int i = 0; i < RT_STACK_WIDTH; i++)
	{
		rd->m_pNewTarget[i] = &rd->m_RTStack[i][0];
		rd->m_pCurTarget[i] = rd->m_pNewTarget[0]->m_pTex;
	}

	rd->CreateMSAADepthBuffer();

	rd->ReleaseAuxiliaryMeshes();
	rd->CreateAuxiliaryMeshes();

	rd->EF_Restore();

	rd->m_bDeviceLost = 0;
	rd->m_pLastVDeclaration = NULL;

#if defined(ENABLE_RENDER_AUX_GEOM)
	if (rd->m_pRenderAuxGeomD3D && FAILED(hr = rd->m_pRenderAuxGeomD3D->RestoreDeviceObjects()))
		return hr;
#endif

	CHWShader_D3D::mfSetGlobalParams();
	//rd->ResetToDefault();

	if (rd->m_OcclQueries.capacity())
	{
		for (int a = 0; a < MAX_OCCL_QUERIES; a++)
			rd->m_OcclQueries[a].Release();
	}

	{
		LOADING_TIME_PROFILE_SECTION_NAMED("CD3D9Renderer::OnD3D10PostCreateDevice(): m_OcclQueries");
		rd->m_OcclQueries.Reserve(MAX_OCCL_QUERIES);
		for (int a = 0; a < MAX_OCCL_QUERIES; a++)
			rd->m_OcclQueries[a].Create();
	}

	return S_OK;
}

#if CRY_PLATFORM_WINDOWS
// Renderer looks for multi-monitor setup changes and fullscreen key combination
bool CD3D9Renderer::HandleMessage(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam, LRESULT* pResult)
{
	switch (message)
	{
	case WM_DISPLAYCHANGE:
	case WM_DEVICECHANGE:
		{
			// Count the number of connected display devices
			bool bHaveMonitorsChanged = true;
			uint connectedMonitors = 0;
			EnumDisplayMonitors(NULL, NULL, CountConnectedMonitors, reinterpret_cast<LPARAM>(&connectedMonitors));

			// Check for changes
			if (connectedMonitors > m_nConnectedMonitors)
			{
				iSystem->GetILog()->LogAlways("[Renderer] A display device has been connected to the system");
			}
			else if (connectedMonitors < m_nConnectedMonitors)
			{
				iSystem->GetILog()->LogAlways("[Renderer] A display device has been disconnected from the system");
			}
			else
			{
				bHaveMonitorsChanged = false;
			}

			// Update state
			m_nConnectedMonitors = connectedMonitors;
			m_bDisplayChanged = bHaveMonitorsChanged;
		}
		break;

	case WM_SYSKEYDOWN:
		{
			const bool bAlt = (lParam & (1 << 29)) != 0;
			if (bAlt)
			{
				if (wParam == VK_RETURN) // ALT+ENTER
				{
					ICVar* const pVar = iConsole->GetCVar("r_fullscreen");
					if (pVar)
					{
						int fullscreen = pVar->GetIVal();
						pVar->Set((int)(fullscreen == 0)); // Toggle CVar
					}
				}
				else if (wParam == VK_MENU)
				{
					// Windows tries to focus the menu when pressing Alt,
					// so we need to tell it that we already handled the event
					*pResult = 0;
					return true;
				}
			}
		}
		break;
	}
	return false;
}
#endif // CRY_PLATFORM_WINDOWS

#if defined(SUPPORT_DEVICE_INFO_USER_DISPLAY_OVERRIDES)
static const char* GetScanlineOrderNaming(DXGI_MODE_SCANLINE_ORDER v)
{
	switch (v)
	{
	case DXGI_MODE_SCANLINE_ORDER_PROGRESSIVE:
		return "progressive";
	case DXGI_MODE_SCANLINE_ORDER_UPPER_FIELD_FIRST:
		return "interlaced (upper field first)";
	case DXGI_MODE_SCANLINE_ORDER_LOWER_FIELD_FIRST:
		return "interlaced (lower field first)";
	default:
		return "unspecified";
	}
}

void UserOverrideDisplayProperties(DXGI_MODE_DESC& desc)
{
	if (gRenDev->m_CVFullScreen->GetIVal())
	{
		if (gRenDev->CV_r_overrideRefreshRate > 0)
		{
			DXGI_RATIONAL& refreshRate = desc.RefreshRate;
			if (refreshRate.Denominator)
				gEnv->pLog->Log("Overriding refresh rate to %.2f Hz (was %.2f Hz).", (float)gRenDev->CV_r_overrideRefreshRate, (float)refreshRate.Numerator / (float)refreshRate.Denominator);
			else
				gEnv->pLog->Log("Overriding refresh rate to %.2f Hz (was undefined).", (float)gRenDev->CV_r_overrideRefreshRate);
			refreshRate.Numerator = (unsigned int) (gRenDev->CV_r_overrideRefreshRate * 1000.0f);
			refreshRate.Denominator = 1000;
		}

		if (gRenDev->CV_r_overrideScanlineOrder > 0)
		{
			DXGI_MODE_SCANLINE_ORDER old = desc.ScanlineOrdering;
			DXGI_MODE_SCANLINE_ORDER& so = desc.ScanlineOrdering;
			switch (gRenDev->CV_r_overrideScanlineOrder)
			{
			case 2:
				so = DXGI_MODE_SCANLINE_ORDER_UPPER_FIELD_FIRST;
				break;
			case 3:
				so = DXGI_MODE_SCANLINE_ORDER_LOWER_FIELD_FIRST;
				break;
			default:
				so = DXGI_MODE_SCANLINE_ORDER_PROGRESSIVE;
				break;
			}
			gEnv->pLog->Log("Overriding scanline order to %s (was %s).", GetScanlineOrderNaming(so), GetScanlineOrderNaming(old));
		}
	}
}
#endif

#include "DeviceInfo.inl"

void EnableCloseButton(void* hWnd, bool enabled)
{
#if CRY_PLATFORM_WINDOWS
	if (hWnd)
	{
		HMENU hMenu = GetSystemMenu((HWND) hWnd, FALSE);
		if (hMenu)
		{
			const unsigned int flags = enabled ? MF_ENABLED : (MF_DISABLED | MF_GRAYED);
			EnableMenuItem(hMenu, SC_CLOSE, MF_BYCOMMAND | flags);
		}
	}
#endif
}

#if defined(SUPPORT_D3D_DEBUG_RUNTIME)
string D3DDebug_GetLastMessage()
{
	return gcpRendD3D->m_d3dDebug.GetLastMessage();
}
#endif
