#include <windows.h>
#include <psapi.h>
#include <d3d11.h>

#include "MinHook.h"

#include "ShaderBytecode.hpp"

#pragma comment (lib, "d3d11.lib")

#include "ReverseEngineeringUtils/Utils/Hash.hpp"
#include "ReverseEngineeringUtils/Utils/Logger.hpp"
#include "ReverseEngineeringUtils/Utils/Utils.hpp"

#include "ReverseEngineeringUtils/Address.hpp"
#include "ReverseEngineeringUtils/Assembly.hpp"
#include "ReverseEngineeringUtils/BaseAddress.hpp"
#include "ReverseEngineeringUtils/Memory.hpp"
#include "ReverseEngineeringUtils/Shader.hpp"

using namespace Fetcko;

// =====================================================
// ==================== Variables ======================
// =====================================================

// Global window hook (for WndProc)
static HHOOK hWindowHook = NULL;

// Main game window
static HWND gameWindow = NULL;

static HWND InitConsole() {
	// We need to set app name during static initialization
	Filesystem::SetAppName("Persona5Ultrawide");

	// Game doesn't have a console window
	AllocConsole();

	// Redirect stdout, stderr, and stdin
	FILE *pCout;
	freopen_s(&pCout, "CONOUT$", "w", stdout);
	freopen_s(&pCout, "CONOUT$", "w", stderr);
	freopen_s(&pCout, "CONIN$", "r", stdin);

	return GetConsoleWindow();
}

// Give us a console window
static HWND consoleWindow = InitConsole();

// Global logger
static LoggableClass *logger = nullptr;

// VTable entries
static void *CreateVertexShader = nullptr;

// Game-specific variables
static float aspectRatio = 16.0f / 9.0f;

// Game-specific code fixes
static std::optional<Address> aspectRatioFix = std::nullopt;
static std::optional<Address> fovFix = std::nullopt;
static Assembly *fovCave = nullptr;

// Game-specific shader fixes
static Shader uiShader("UI", { 0xE68DC32F, 0xE55C3C7A, 0x2D64D798, 0x803DFB07 });
static Shader selectionShader("Selection", { 0x33EA162E, 0x7AEDE1E2, 0xF95DA93, 0x93CD21E0 });

// =====================================================
// ==================== DX11 Hooks =====================
// =====================================================
typedef HRESULT(__stdcall *CreateVertexShader_t)(
	ID3D11Device *pDevice, const void *pShaderBytecode, SIZE_T BytecodeLength,
	ID3D11ClassLinkage *pClassLinkage, ID3D11VertexShader **ppVertexShader);

CreateVertexShader_t oCreateVertexShader = nullptr;

HRESULT __stdcall hkCreateVertexShader(
	ID3D11Device *pDevice, const void *pShaderBytecode, SIZE_T BytecodeLength,
	ID3D11ClassLinkage *pClassLinkage, ID3D11VertexShader **ppVertexShader) {
	std::optional<HRESULT> ret = std::nullopt;

	for (auto shader : Shader::GetTargetShaders()) {
		if (shader->IsMatch(pShaderBytecode, BytecodeLength)) {
			if (shader->GetName() == "UI") {
				// Adjust width
				shader->PatchIndices<float>({ 912, 1100, 1468 }, 2.0f, aspectRatio / (16.0f / 9.0f));
				// Adjust lefthand side
				shader->PatchIndices<float>({ 1656 }, 0.5f, 1.0f - (1.0f / (aspectRatio / (16.0f / 9.0f))));
			} else if (shader->GetName() == "Selection") {
				// Adjust width
				shader->PatchIndices<float>({ 824, 1012, 1412 }, 2.0f, aspectRatio / (16.0f / 9.0f));
				// Adjust lefthand side
				shader->PatchIndices<float>({ 1600 }, 0.5f, 1.0f - (1.0f / (aspectRatio / (16.0f / 9.0f))));
			}

			ret = oCreateVertexShader(pDevice, shader->GetBytecode(), shader->GetBytecodeSize(), pClassLinkage, ppVertexShader);

			// Don't need the new bytecode anymore
			shader->ClearBytecode();

			// ClearBytecode() removes this shader from TargetShaders
			break;
		}
	}

	if (!ret)
		ret = oCreateVertexShader(pDevice, pShaderBytecode, BytecodeLength, pClassLinkage, ppVertexShader);

	return *ret;
}

// =====================================================
// =================== Window Hooks ====================
// =====================================================
LRESULT CALLBACK CallWndProc(int nCode, WPARAM wParam, LPARAM lParam) {
	static int count = 0;
	if (nCode >= 0) {
		CWPSTRUCT *pCwp = reinterpret_cast<CWPSTRUCT *>(lParam);

		if (pCwp->message == WM_CREATE && oCreateVertexShader == nullptr) {
			DXGI_SWAP_CHAIN_DESC desc = { 0 };
			desc.BufferCount = 1;
			desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
			desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
			desc.OutputWindow = pCwp->hwnd;
			desc.SampleDesc.Count = 1;
			desc.Windowed = TRUE;

			ID3D11Device *pDummyDevice = nullptr;
			ID3D11DeviceContext *pDummyContext = nullptr;
			IDXGISwapChain *pDummySwapChain = nullptr;
			D3D_FEATURE_LEVEL featLevel;

			HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
				nullptr, 0, D3D11_SDK_VERSION, &desc, &pDummySwapChain, &pDummyDevice, &featLevel, &pDummyContext);

			if (SUCCEEDED(hr)) {
				CreateVertexShader = (*(void ***)pDummyDevice)[12];

				if (MH_CreateHook(CreateVertexShader, &hkCreateVertexShader, reinterpret_cast<LPVOID *>(&oCreateVertexShader)) == MH_OK) {
					MH_EnableHook(MH_ALL_HOOKS);
					if (logger)
						logger->LogDebug("Hooked CreateVertexShader!");
				} else if (logger)
					logger->LogError("Could not hook CreateVertexShader!");

				pDummySwapChain->Release();
				pDummyDevice->Release();
				pDummyContext->Release();
			} else {
				if (logger)
					logger->LogError("Could not create device and swap chain: ", std::hex, hr);
			}
		} else if (pCwp->message == WM_SIZE) {
			int width = LOWORD(pCwp->lParam);
			int height = HIWORD(pCwp->lParam);

			if (width && height && (!gameWindow || gameWindow == pCwp->hwnd)) {
				if (!gameWindow)
					gameWindow = pCwp->hwnd;

				aspectRatio = static_cast<float>(width) / height;

				if (aspectRatioFix)
					aspectRatioFix->Set<float>(aspectRatio);
				if (fovCave)
					fovCave->Update("AspectRatio", aspectRatio);

				if (logger)
					logger->LogDebug("Window resized to ", width, " x ", height, " (", aspectRatio, ")");
			}
		}
	}

	return CallNextHookEx(hWindowHook, nCode, wParam, lParam);
}

// =====================================================
// ==================== DLL Hooks ======================
// =====================================================
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
	LoggableClass loggableClass("DllMain");

	switch (ul_reason_for_call) {
	case DLL_PROCESS_ATTACH: {
		if (HMODULE modules[1024]; K32EnumProcessModules(GetCurrentProcess(), modules, sizeof(modules), &ul_reason_for_call)) {
			for (DWORD i = 0; i < std::min<DWORD>(ul_reason_for_call / sizeof(HMODULE), ARRAYSIZE(modules)); ++i) {
				if (modules[i] != hModule && GetProcAddress(modules[i], "DInput84L2Version") != nullptr) {
					return FALSE;
				}
			}
		}

		logger = new LoggableClass(
			Filesystem::GetAppName()
		);

		uiShader.SetBytecode(UiShaderBytes, sizeof(UiShaderBytes));
		selectionShader.SetBytecode(SelectionShaderBytes, sizeof(SelectionShaderBytes));

		MH_Initialize();

		hWindowHook = SetWindowsHookExA(
			WH_CALLWNDPROC,
			CallWndProc,
			NULL,
			GetCurrentThreadId()
		);

		const auto baseAddress = BaseAddress::Get();

		loggableClass.LogDebug("Base address = 0x", std::hex, baseAddress.address);

		// Aspect Ratio Fix
		if (aspectRatioFix =
			Memory::SignatureScan(
				"F3 0F 59 ?? ?? ?? ?? ?? F3 48 ?? ?? ?? 3B ?? 72 ?? 0F ?? ?? F3 48 ?? ?? ?? F3 0F",
				baseAddress.address,
				baseAddress.sizeOfCode,
				0x19
			);
			aspectRatioFix->GetAddress() != baseAddress.address) {
			aspectRatioFix->JumpToOffsetAtByte(4);

			loggableClass.LogDebug("Old aspect ratio = ", aspectRatioFix->GetFloat());

			aspectRatioFix->Set<float>(32.0f / 9.0f);
		}

		// Scene change FoV
		// Quickest way to trigger is first entry into Leblanc
		if (fovFix =
			Memory::SignatureScan(
				"33 ?? 0F 28 ?? F3 0F ?? ?? F3 0F ?? ?? 0F 28 ?? F3 0F 5E ?? ?? ?? ?? ?? 48 89 ?? ?? 48 89 ?? ?? F3",
				baseAddress.address,
				baseAddress.sizeOfCode,
				0x10
			);
			fovFix->GetAddress() != baseAddress.address) {

			fovCave = new Assembly(baseAddress, *fovFix);
			if (fovCave) {
				fovCave->Allocate<float>("TempValue1");
				fovCave->Allocate<float>("AspectRatio", 32.0f / 9.0f);

				if (fovCave->Assemble(
					R"(
						movss [TempValue1], $$1
						movss $$1, [AspectRatio]
						movss [$$2], $$1
						movss $$1, [TempValue1]
					)"
				)) {
					fovCave->EnableCodeCave(true);
//					fovCave->Monitor(true);
				}
			}
		}

		break;
	} case DLL_PROCESS_DETACH:
		if (lpReserved == nullptr) {
			if (fovCave) {
//				fovCave->Monitor(false);
				fovCave->Deallocate();

				delete fovCave;
				fovCave = nullptr;
			}

			if (logger) {
				delete logger;
				logger = nullptr;
			}

			if (CreateVertexShader)
				MH_DisableHook(CreateVertexShader);
		}

		break;
	}

	return TRUE;
}
