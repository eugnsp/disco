#include <Windows.h>
#include "resource.h"
#include <ShObjIdl.h>
#include <atlbase.h>
#include <atlstr.h>
#include <cstdlib>
#include <utility>
#include <vector>
#include <fstream>
#include <string>

#pragma warning (disable: 4101 4996)

HINSTANCE dll_handle;
const UINT cmd_colors_dialog = 599;
std::vector<std::pair<CString, DWORD>> colors;

DWORD main_thread_id;
DWORD tls_index = TLS_OUT_OF_INDEXES;

HWND main_wnd;
HMENU main_menu;
WNDPROC wndproc_disco;

bool is_panels_found = false;
HWND panel_l;
HWND panel_r;
WNDPROC wndproc_panel_l;
WNDPROC wndproc_panel_r;

const BYTE LEFT_PANEL = 1;
const BYTE RIGHT_PANEL = 2;

const BYTE SORT_NAME = 0;
const BYTE SORT_EXT = 1;

// Colorizer
char* filename_ext;

// Windows >=7 task bar
HWND wnd_progress_bar;
HWND wnd_progress_bar_parent;
BYTE progress;
UINT WM_TASKBARBUTTONCREATED = 0;
CComPtr<ITaskbarList3> taskbar_list;

CString path_main_ini_file;
CString path_colors_ini_file;

char fixed_font[LF_FACESIZE + 30];

// Setup colors dialog
decltype(colors) colors_tmp_in_dlg;
HWND wnd_colors_sel;

// Rename file dialog hack
bool is_main_window_hidden = false;
HWND wnd_dialog;
const BYTE IDC_BNNAME = 100;
WNDPROC wndproc_rename_dialog_old;
WNDPROC wndproc_rename_dialog_edit_box_old;

bool is_title_changed = false;

// Converts string description of a fixed font into LOGFONTA structure
using GetLogFontFunc = void(*)(LPSTR, DWORD, LOGFONTA*);
const GetLogFontFunc GetLogFont = reinterpret_cast<GetLogFontFunc>(0x46C340);
LOGFONTA lf_fixed_font;

// ----------------------------------------

void get_window_text(HWND wnd, CString& text)
{
	const auto len = GetWindowTextLengthA(wnd);
	GetWindowTextA(wnd, text.GetBufferSetLength(len), len + 1);
	text.ReleaseBuffer();
}

void get_class_name(HWND wnd, CString& text)
{
	const int len = 255;
	GetClassNameA(wnd, text.GetBufferSetLength(len), len + 1);
	text.ReleaseBuffer();
}

bool char_ci_comp(char a, char b)
{
	return tolower(a) == tolower(b);
}

// ----------------------------------------

__declspec(naked) void hook_colorizer() noexcept
{
	char* filename;
	DWORD color, back_color;
	bool current, selected, panel;

	__asm
	{
		// ebx - pointer to the filename; edx - text color; edi - text background color
		push ebp
		mov ebp, esp
		sub esp, __LOCAL_SIZE
		mov filename, ebx				// pointer to the C-string of the filename string (as displayed)
		and edx, 0xFFFFFF
		mov color, edx					// foreground color
		mov back_color, edi
		pushad
		mov al, [ebp + 0xAC + 12]		// current flag (+8 - stack correction of CALL and PUSH ebp)
		mov current, al
		mov eax, [ebp + 0xAC + 8]		// selected flag (+8 - stack correction of CALL and PUSH ebp)
		mov cl, [eax + 0x10]
		mov selected, cl
		mov eax, [ebp]					// [ebp] = old ebp
		mov cl, [eax + 0x94]			// panel flag
		mov panel, cl
	}

	if (!(filename[0] == '.' && filename[1] == '.' && filename[2] == ' ') &&
		filename_ext && !selected /*&& (!panel || !current)*/)
	{
		const auto ext_len = strlen(filename_ext);
		
		if (ext_len > 0)
			for (const auto& clr : colors)
			{
				bool last = false;

				const CHAR* start = clr.first;
				auto* end = strchr(start, ',');
				if (!end)
				{
					end = strchr(start, 0);
					last = true;
				}

				for (;;)
				{
					if (std::equal(start, end, filename_ext, filename_ext + ext_len, char_ci_comp))
					{
						(current && panel ? back_color : color) = clr.second;
						goto COLOR_SEARCH_BREAK;
					}

					if (last)
						break;

					start = end + 1;
					if (!(end = strchr(start, ',')))
					{
						end = strchr(start, 0);
						last = true;
					}
				}
			}
	}

	COLOR_SEARCH_BREAK:
	__asm
	{
		popad
		mov edx, color
		mov edi, back_color
		mov esp, ebp
		pop ebp
		ret
	}
}

__declspec(naked) void hook_get_filename() noexcept
{
	__asm
	{
		mov esi, [esp + 0x10 + 4]	// +4 - stack correction of CALL
		mov edi, [esi + 30h]
		mov filename_ext, edi

		// Restore overwritten commands
		mov esi, eax
		mov eax, ecx
		mov edi, edx
		ret
	}
}

__declspec(naked) void hook_progress() noexcept
{
	__asm
	{
		mov progress, al

		// Restore overwritten commands
		mov [esp + 0x1C + 4], eax	// +4 - stack correction of CALL
		test eax, eax
		ret
	}
}

void ReadDiscoIniFile()
{
	//std::ifstream file(pathMainIniFile);
	//if (!file)
	//	return;

	//HANDLE hFile = CreateFile(sDiscoIniPath, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
	//if (INVALID_HANDLE_VALUE == hFile)
	//	return;

	//DWORD dwSize = GetFileSize(hFile, 0);
	//if (!dwSize)
	//{
	//	CloseHandle(hFile);
	//	return;
	//}

	//char *buffer = (char*)malloc(dwSize);
	//DWORD dwBytesRead;
	//if (ReadFile(hFile, buffer, dwSize, &dwBytesRead, 0) == FALSE)
	//{
	//	CloseHandle(hFile);
	//	free(buffer);
	//	return;
	//}

	//CloseHandle(hFile);

	//if (dwBytesRead <= 0)
	//{
	//	free(buffer);		
	//	return;
	//}

	//// fixed font=
	//memset(sFixedFont, 0, sizeof(sFixedFont));
	//DWORD pos = 0;

	//if (char *ff = strstr(buffer, "fixed font="))
	//{
	//	ff += 11;
	//	pos = ff - buffer;
	//	DWORD oldPos = pos;

	//	while (!(0x0D == buffer[pos] || 0x0A == buffer[pos]) && pos < dwBytesRead)
	//		pos++;

	//	memcpy_s(sFixedFont, sizeof(sFixedFont), ff, pos - oldPos);
	//	GetLogFont(sFixedFont, 0, &lfFixedFont);
	//}

	//free(buffer);
	//return;
}

bool read_colors_ini_file()
{
	std::ifstream file(path_colors_ini_file);
	if (!file)
	{
		MessageBoxA(0, "Configuration file cannot be read!", "Disco Plus", MB_OK | MB_ICONERROR);
		return false;
	}

	colors.clear();

	std::string line;
	while (std::getline(file, line))
	{
		const auto pos = line.find('=');
		if (pos == std::string::npos || pos >= line.length() - 6)
			continue;
		
		std::string extensions(line, 0, pos);
		std::string color(line, pos + 1, 6);
		
		DWORD clr = strtol(color.c_str(), nullptr, 16);
		clr = ((clr & 0xFF) << 16) | (clr & 0xFF00) | ((clr & 0xFF0000) >> 16);

		colors.emplace_back(extensions.c_str(), clr);
	}

	return true;
}

bool write_colors_ini_file()
{
	std::ofstream file(path_colors_ini_file);
	if (!file)
	{
		MessageBoxA(0, "Configuration file cannot be written!", "Disco Plus", MB_OK | MB_ICONERROR);
		return false;
	}

	for (const auto& color : colors)
	{
		char colorString[7];
		sprintf_s(colorString, 7, "%02x%02x%02x", color.second & 0xFF, (color.second >> 8) & 0xFF, (color.second >> 16) & 0xFF);
		
		file << color.first << '=' << colorString << std::endl;
	}

	return true;
}

void update_panel(BYTE panel)
{
	if (panel & LEFT_PANEL)
		SendMessageA(main_wnd, WM_COMMAND, 217, 0);

	if (panel & RIGHT_PANEL)
		SendMessageA(main_wnd, WM_COMMAND, 317, 0);
}

void set_sort_order(BYTE panel, BYTE ord)
{
	SendMessageA(main_wnd, WM_COMMAND, (ord == SORT_EXT ? 213 : 212) + (panel == RIGHT_PANEL ? 100 : 0), 0);
}

void init_color_dialog(HWND wnd)
{
	colors_tmp_in_dlg = colors;

	const auto lbExt = GetDlgItem(wnd, IDC_EXTLIST);
	for (auto &color : colors_tmp_in_dlg)
		SendMessageA(lbExt, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(color.first.GetBuffer()));
	
	SendMessageA(lbExt, LB_SETCURSEL, 0, 0);
	if (!colors_tmp_in_dlg.empty())
		SetDlgItemTextA(wnd, IDC_EDITEXT, colors_tmp_in_dlg.front().first);

	RECT bu = {156, 30, 108, 24};
	MapDialogRect(wnd, &bu);
	wnd_colors_sel = CreateWindowExA(0, "STATIC", 0, WS_CHILD | WS_VISIBLE | SS_OWNERDRAW, 
            bu.left, bu.top, bu.right, bu.bottom, wnd, 0, 0, nullptr);
}

void end_color_dialog(HWND wnd, bool save)
{
	EndDialog(wnd, 0);

	if (save)
	{
		colors = colors_tmp_in_dlg;
		update_panel(LEFT_PANEL | RIGHT_PANEL);
		write_colors_ini_file();
	}
}

LRESULT CALLBACK wndproc_dlg_colors(HWND wnd, UINT msg, WPARAM w_param, LPARAM l_param)
{
	static COLORREF custClr[16];
	static LONG index = 0;
	
	switch (msg)
	{
	case WM_INITDIALOG:
		index = 0;
		init_color_dialog(wnd);
		return TRUE;

	case WM_COMMAND:
		if (LOWORD(w_param) == IDC_EXTLIST && HIWORD(w_param) == LBN_SELCHANGE)
		{
			index = SendMessageA(reinterpret_cast<HWND>(l_param), LB_GETCURSEL, 0, 0);
			if (index == LB_ERR)
				break;
			if (index < static_cast<LONG>(colors_tmp_in_dlg.size()))
			{
				SetDlgItemTextA(wnd, IDC_EDITEXT, colors_tmp_in_dlg[index].first);
				InvalidateRect(wnd, 0, 0);
			}
			return TRUE;
		}

		// Select color
		if (LOWORD(w_param) == IDC_BNSELECT && HIWORD(w_param) == BN_CLICKED && index >= 0)
		{
			CHOOSECOLOR clr;
			memset(&clr, 0, sizeof(clr));
			clr.lStructSize = sizeof(clr);
			clr.hwndOwner = wnd;
			clr.lpCustColors = custClr;
			if (!colors_tmp_in_dlg.empty())
				clr.rgbResult = colors_tmp_in_dlg[index].second;
			clr.Flags = CC_RGBINIT | CC_FULLOPEN;

			if (ChooseColorA(&clr))
			{
				if (!colors_tmp_in_dlg.empty())
					colors_tmp_in_dlg[index].second = clr.rgbResult;

				InvalidateRect(wnd, 0, 0);
			}
			return TRUE;
		}

		// Change
		if (LOWORD(w_param) == IDC_BNCHANGE && HIWORD(w_param) == BN_CLICKED && index >= 0)
		{
			auto len = GetWindowTextLengthA(GetDlgItem(wnd, IDC_EDITEXT));
			CString str;
			GetDlgItemTextA(wnd, IDC_EDITEXT, str.GetBuffer(len), len + 1);
			str.ReleaseBuffer();

			colors_tmp_in_dlg.at(index).first = str;

			auto lbExt = GetDlgItem(wnd, IDC_EXTLIST);
			SendMessageA(lbExt, LB_DELETESTRING, index, 0);
			SendMessageA(lbExt, LB_INSERTSTRING, index, reinterpret_cast<LPARAM>(str.GetBuffer()));
			SendMessageA(GetDlgItem(wnd, IDC_EXTLIST), LB_SETCURSEL, index, 0);
			return TRUE;
		}

		// Add
		if (LOWORD(w_param) == IDC_BNADD && HIWORD(w_param) == BN_CLICKED)
		{
			auto len = GetWindowTextLengthA(GetDlgItem(wnd, IDC_EDITEXT));
			CString str;
			GetDlgItemTextA(wnd, IDC_EDITEXT, str.GetBuffer(len), len + 1);
			str.ReleaseBuffer();

			colors_tmp_in_dlg.emplace_back(str, 0);

			index = colors_tmp_in_dlg.size() - 1;
			SendMessageA(GetDlgItem(wnd, IDC_EXTLIST), LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(str.GetBuffer()));
			SendMessageA(GetDlgItem(wnd, IDC_EXTLIST), LB_SETCURSEL, index, 0);
			InvalidateRect(wnd, 0, 0);
			return TRUE;
		}

		// Delete
		if (LOWORD(w_param) == IDC_BNDEL && HIWORD(w_param) == BN_CLICKED && index >= 0)
		{
			auto lbExt = GetDlgItem(wnd, IDC_EXTLIST);
			SendMessageA(lbExt, LB_DELETESTRING, index, 0);
			
			colors_tmp_in_dlg.erase(colors_tmp_in_dlg.cbegin() + index);

			if (index >= static_cast<LONG>(colors_tmp_in_dlg.size()))
				--index;

			SendMessageA(GetDlgItem(wnd, IDC_EXTLIST), LB_SETCURSEL, index, 0);
			InvalidateRect(wnd, 0, 0);
			return TRUE;
		}

		if (LOWORD(w_param) == IDCANCEL && HIWORD(w_param) == BN_CLICKED)
		{
			end_color_dialog(wnd, false);
			return TRUE;
		}

		if (LOWORD(w_param) == IDOK && HIWORD(w_param) == BN_CLICKED)
		{
			end_color_dialog(wnd, true);
			return TRUE;
		}

		break;

	case WM_DRAWITEM:
		const auto dis = reinterpret_cast<const DRAWITEMSTRUCT*>(l_param);
		if (dis->hwndItem == wnd_colors_sel)
		{
			auto br = CreateSolidBrush(RGB(0x33, 0x33, 0x33));
			FillRect(dis->hDC, &dis->rcItem, br);
			DeleteObject(br);
			if (!colors_tmp_in_dlg.empty() && index >= 0)
			{
				SetTextColor(dis->hDC, colors_tmp_in_dlg[index].second);
				SetBkColor(dis->hDC, RGB(0x33, 0x33, 0x33));

				auto font = CreateFontIndirectA(&lf_fixed_font);
				if (font)
					SelectObject(dis->hDC, font);

				auto rect = dis->rcItem;
				DrawTextExA(dis->hDC, "filename.ext", -1, &rect, DT_CENTER | DT_SINGLELINE | DT_VCENTER, 0);
				DeleteObject(font);
			}
			return TRUE;
		}

		break;
	}

	//return DefWindowProc(hwnd, uMsg, w_param, l_param);
	return FALSE;
}

LRESULT CALLBACK wndprocRenameDialogEditBox(HWND wnd, UINT msg, WPARAM w_param, LPARAM l_param)
{
	if (msg == WM_KEYDOWN && w_param == VK_F6)
	{
		const auto filename = reinterpret_cast<const CHAR*>(0x5364B0);
		SetDlgItemTextA(wnd_dialog, 0xFA1, filename);
		return TRUE;
	}

	if (msg == WM_KEYDOWN && w_param == VK_F9 && !is_main_window_hidden)
	{
		ShowWindow(main_wnd, SW_HIDE);

		RECT r;
		GetWindowRect(wnd_dialog, &r);
		SetWindowPos(wnd_dialog, HWND_TOPMOST, r.left, r.top, r.right - r.left, r.bottom - r.top, SWP_SHOWWINDOW);

		is_main_window_hidden = true;
		return TRUE;
	}

	return CallWindowProc(wndproc_rename_dialog_edit_box_old, wnd, msg, w_param, l_param);
}

LRESULT CALLBACK wndproc_rename_dialog(
	const HWND wnd, const UINT msg, const WPARAM w_param, const LPARAM l_param)
{
	if (msg == WM_COMMAND && LOWORD(w_param) == IDC_BNNAME && HIWORD(w_param) == BN_CLICKED)
	{
		CHAR* filename = reinterpret_cast<CHAR*>(0x5364B0);
		SetDlgItemTextA(wnd, 0xFA1, filename);
		return TRUE;
	}

	if (msg == WM_DESTROY && is_main_window_hidden)
		ShowWindow(main_wnd, SW_SHOW);

	return CallWindowProc(wndproc_rename_dialog_old, wnd, msg, w_param, l_param);
}

LRESULT CALLBACK call_wndproc(const UINT msg, const WPARAM w_param, const LPARAM l_param)
{
	if (msg < 0)
		return CallNextHookEx(0, msg, w_param, l_param);

	if (msg == HC_ACTION && taskbar_list)
	{
		const auto msg = reinterpret_cast<const CWPSTRUCT*>(l_param);
		if (msg->message == WM_CREATE && !wnd_progress_bar)
		{
			CString title, wclass;
		
			get_window_text(msg->hwnd, title);
			get_class_name(msg->hwnd, wclass);
			
			if (wclass == "Static" && title == "progress indicator")
			{
				// Got progress indicator window handle				
				wnd_progress_bar = msg->hwnd;
				wnd_progress_bar_parent = GetParent(wnd_progress_bar);
				taskbar_list->SetProgressState(wnd_progress_bar_parent, TBPF_NORMAL);
				progress = 0;
			}
		}

		if (msg->message == WM_DESTROY && wnd_progress_bar == msg->hwnd)
		{
			taskbar_list->SetProgressState(wnd_progress_bar_parent, TBPF_NOPROGRESS);
			wnd_progress_bar = 0;
		}

		if (((msg->message == WM_PAINT && wnd_progress_bar == msg->hwnd) ||
			(msg->message == WM_SETTEXT && wnd_progress_bar_parent == msg->hwnd)) &&
			progress >= 0 && progress <= 100)
		{
			taskbar_list->SetProgressValue(wnd_progress_bar_parent, progress, 100);
		}
	}

	return CallNextHookEx(0, msg, w_param, l_param);
}

LRESULT CALLBACK wndproc_disco_plus(HWND wnd, UINT msg, WPARAM w_param, LPARAM l_param)
{
	if (!is_title_changed)
	{
		is_title_changed = true;

		CString title;
		get_window_text(main_wnd, title);
		SetWindowTextA(main_wnd, title + "++");
	}

	if (WM_TASKBARBUTTONCREATED == msg)
	{
		const auto ver = GetVersion();
		const auto major = LOBYTE(LOWORD(ver));
		const auto minor = HIBYTE(LOWORD(ver));

		if (major > 6 || (major == 6 && minor > 0))
		{
			if (taskbar_list)
				return TRUE;

			CoInitialize(0);
			auto hr = taskbar_list.CoCreateInstance(CLSID_TaskbarList, nullptr, CLSCTX_INPROC_SERVER);
			if (hr != S_OK)
			{
				taskbar_list.Release();
				CoUninitialize();
			}
		}

		return TRUE;
	}

	if (WM_ACTIVATE == msg && WA_INACTIVE == LOWORD(w_param))
	{
		wnd_dialog = reinterpret_cast<HWND>(l_param);
		if (wnd_dialog)
		{
			CString title, wclass;

			get_window_text(wnd_dialog, title);
			get_class_name(wnd_dialog, wclass);

			// Rename file dialog
			if (wclass == "#32770" && (title == "Переименование" || title == "Rename"))
			{
				const auto butt_rename = GetDlgItem(wnd_dialog, 1);
				const auto butt_tree = GetDlgItem(wnd_dialog, 0xFA7);
				const auto butt_cancel = GetDlgItem(wnd_dialog, 2);
				if (butt_rename && butt_tree && butt_cancel)
				{
					SetWindowPos(butt_rename, 0, 14, 89, 101, 23, 0);
					SetWindowPos(butt_tree, 0, 122, 89, 95, 23, 0);
					SetWindowPos(butt_cancel, 0, 291, 89, 70, 23, 0);
					const auto font = reinterpret_cast<HFONT>(SendMessageA(butt_rename, WM_GETFONT, 0, 0));
					const auto butt_name = CreateWindowExA(WS_EX_NOPARENTNOTIFY, "Button", title == "Rename" ? "Name" : "Имя",
						WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON | BS_TEXT, 224, 89, 59, 23, wnd_dialog, reinterpret_cast<HMENU>(IDC_BNNAME), 0, 0);
					SendMessageA(butt_name, WM_SETFONT, reinterpret_cast<WPARAM>(font), 0);
				}
				is_main_window_hidden = false;
				wndproc_rename_dialog_old = reinterpret_cast<WNDPROC>(SetWindowLongA(wnd_dialog, GWL_WNDPROC, reinterpret_cast<LONG>(wndproc_rename_dialog)));
				auto edit_box_wnd = GetDlgItem(GetDlgItem(wnd_dialog, 0xFA1), 0x3E9);
				wndproc_rename_dialog_edit_box_old = reinterpret_cast<WNDPROC>(SetWindowLongA(edit_box_wnd, GWL_WNDPROC, reinterpret_cast<LONG>(wndprocRenameDialogEditBox)));
			}
		}
	}

	if (msg == WM_COMMAND && w_param == cmd_colors_dialog && l_param == 0)
	{
		DialogBox(dll_handle, MAKEINTRESOURCE(IDD_DLGCOLORS), wnd, reinterpret_cast<DLGPROC>(wndproc_dlg_colors));
		return TRUE;
	}

	LRESULT r = CallWindowProc(wndproc_disco, wnd, msg, w_param, l_param);
	
	// Fix our item to be ENABLED - overwrite MFC mechanism
	if (msg == WM_INITMENUPOPUP && l_param == 5)
		EnableMenuItem(reinterpret_cast<HMENU>(w_param), cmd_colors_dialog, MF_ENABLED);
	
	return r;
}

LRESULT WndProcPanelSPlus(BYTE panel, HWND wnd, UINT msg, WPARAM w_param, LPARAM l_param)
{
	if (msg == WM_XBUTTONDOWN)
	{
		set_sort_order(panel, XBUTTON2 == GET_XBUTTON_WPARAM(w_param) ? SORT_NAME : SORT_EXT);
		return TRUE;
	}

	if ((msg == WM_LBUTTONDOWN || msg == WM_RBUTTONDOWN) && w_param == (MK_LBUTTON | MK_RBUTTON))
	{
		update_panel(panel);
		return TRUE;
	}

	LRESULT r = CallWindowProc(panel == RIGHT_PANEL ? wndproc_panel_r : wndproc_panel_l, wnd, msg, w_param, l_param);	
	return r;
}

LRESULT CALLBACK WndProcPanelLPlus(HWND hwnd, UINT uMsg, WPARAM w_param, LPARAM l_param)
{
	return WndProcPanelSPlus(LEFT_PANEL, hwnd, uMsg, w_param, l_param);
}

LRESULT CALLBACK WndProcPanelRPlus(HWND hwnd, UINT uMsg, WPARAM w_param, LPARAM l_param)
{
	return WndProcPanelSPlus(RIGHT_PANEL, hwnd, uMsg, w_param, l_param);
}

bool set_hook(HANDLE proc, DWORD address, LONGLONG old_code, LPCVOID hook_proc_addr)
{
	BYTE hook_code[6] = {0xE8, 0, 0, 0, 0, 0x90}; // CALL [relative addr], NOP
	*((DWORD*)&hook_code[1]) = reinterpret_cast<DWORD>(hook_proc_addr) - address - 5 /* CALL instruction size */;

	if (*reinterpret_cast<const LONGLONG*>(address) == old_code)
		return WriteProcessMemory(proc, reinterpret_cast<LPVOID>(address), &hook_code, sizeof(hook_code), 0) == TRUE;
	else // Bad signature
		return false;
}

void patch_word(HANDLE proc, DWORD address, WORD old_code, WORD new_code)
{
	if (*reinterpret_cast<const WORD*>(address) == old_code)
		WriteProcessMemory(proc, reinterpret_cast<LPVOID>(address), &new_code, sizeof(new_code), 0);
}

BOOL APIENTRY DllMain(HINSTANCE module, DWORD reason, LPVOID)
{
	switch (reason)
	{
	case DLL_THREAD_ATTACH:
		{
			HHOOK hook_disco_plus = 0;
			if (main_thread_id)
				hook_disco_plus = SetWindowsHookExA(
					WH_CALLWNDPROC, reinterpret_cast<HOOKPROC>(call_wndproc),
					0, GetCurrentThreadId());

			TlsSetValue(tls_index, hook_disco_plus);
		}

		if (!main_wnd)
		{
			const auto pid = GetCurrentProcessId();
			main_wnd = GetTopWindow(0);
			while (main_wnd)
			{
				if (GetWindowLongA(main_wnd, GWL_STYLE) & WS_VISIBLE)
				{
					DWORD wpid;
					GetWindowThreadProcessId(main_wnd, &wpid);
					if (wpid == pid)
					{
						main_thread_id = GetCurrentThreadId();

						taskbar_list = 0;
						WM_TASKBARBUTTONCREATED = RegisterWindowMessageA("TaskbarButtonCreated");

						wndproc_disco = reinterpret_cast<WNDPROC>(SetWindowLongA(main_wnd, GWL_WNDPROC, reinterpret_cast<LONG>(wndproc_disco_plus)));

						main_menu = GetMenu(main_wnd);
						InsertMenuA(GetSubMenu(main_menu, 5), 6, MF_BYPOSITION | MF_STRING, cmd_colors_dialog, "Цвета расширений...");
						break;
					}
				}
				main_wnd = GetWindow(main_wnd, GW_HWNDNEXT);
			}
			if (!main_menu)
				main_wnd = 0;
		}

		if (!(panel_l && panel_r) && main_wnd)
		{
			const auto client = GetWindow(main_wnd, GW_CHILD);
			if (client)
			{
				panel_l = FindWindowExA(client, 0, "DISCoWindowClass00536C00", "Panel");
				if (!panel_l)
					break;

				panel_r = FindWindowExA(client, panel_l, "DISCoWindowClass00536C00", "Panel");
				if (!panel_r)
					break;

				if (GetDlgCtrlID(panel_l) != 1)	// Left panel has Control ID = 1
					std::swap(panel_l, panel_r);

				wndproc_panel_l = reinterpret_cast<WNDPROC>(SetWindowLongA(panel_l, GWL_WNDPROC, reinterpret_cast<LONG>(WndProcPanelLPlus)));
				wndproc_panel_r = reinterpret_cast<WNDPROC>(SetWindowLongA(panel_r, GWL_WNDPROC, reinterpret_cast<LONG>(WndProcPanelRPlus)));
			}

		}

		break;

	case DLL_THREAD_DETACH:
		{
			const auto hook_disco_plus = reinterpret_cast<HHOOK>(TlsGetValue(tls_index));
			if (hook_disco_plus)
				UnhookWindowsHookEx(hook_disco_plus);
		}
		break;

	case DLL_PROCESS_ATTACH:
		dll_handle = module;
		tls_index = TlsAlloc();

		if (tls_index == TLS_OUT_OF_INDEXES)
			return FALSE;

		{
			if (!GetModuleFileNameA(0, path_colors_ini_file.GetBuffer(MAX_PATH), MAX_PATH))
				return FALSE;

			path_colors_ini_file.ReleaseBuffer();
			const auto pos = path_colors_ini_file.ReverseFind('\\');
			if (pos == -1)
				return FALSE;

			path_colors_ini_file = path_colors_ini_file.Left(pos);
			path_main_ini_file = path_colors_ini_file;

			path_colors_ini_file += "\\colors.ini";			
			path_main_ini_file += "\\dc.ini";

			ReadDiscoIniFile();
			if (!read_colors_ini_file())
				return FALSE;

			const auto proc = OpenProcess(PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_VM_OPERATION, 0, GetCurrentProcessId());
			if (!proc)
				return FALSE;

			// Hook: get filename
			if (!set_hook(proc, 0x43DB5D, 0xE9C1FA8BC18BF08B, &hook_get_filename))
			{
				CloseHandle(proc);
				return FALSE;
			}

			// Hook: file colorizer
			if (!set_hook(proc, 0x43537E, 0x8B5200FFFFFFE281, &hook_colorizer))
			{
				CloseHandle(proc);
				return FALSE;
			}

			// Hook: progress bar value
			if (!set_hook(proc, 0x464E8E, 0x840F1C244489C085, &hook_progress))
			{
				CloseHandle(proc);
				return FALSE;
			}

			// Disable uppercase for directories:
			// 1) in the panels - change JNZ (0f 85 [cd]) to NOP, JMP (90, e9 [cd])
			// 2) in the status bar - change JNZ (75 [cb]) to JMP (eb [cb])
			patch_word(proc, 0x43DBFB, 0x850F, 0xE990);
			patch_word(proc, 0x43DD2B, 0x3275, 0x32EB);

			CloseHandle(proc);
		}

		break;

	case DLL_PROCESS_DETACH:
		if (taskbar_list)
		{
			taskbar_list.Release();
			CoUninitialize();
		}

		if (wndproc_panel_l)
			SetWindowLongA(panel_l, GWL_WNDPROC, reinterpret_cast<LONG>(wndproc_panel_l));

		if (wndproc_panel_r)
			SetWindowLongA(panel_r, GWL_WNDPROC, reinterpret_cast<LONG>(wndproc_panel_r));

		if (wndproc_disco)
			SetWindowLongA(main_wnd, GWL_WNDPROC, reinterpret_cast<LONG>(wndproc_disco));

		TlsFree(tls_index);

		break;
	}

	return TRUE;
}
