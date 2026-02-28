/*
 * WorkTimer v2.0
 * wxWidgets + Win32 API
 * - Process name (.exe) based detection
 * - System tray icon
 * - Onboarding wizard
 * - App list with icons
 */

#include "wx/wxprec.h"
#ifndef WX_PRECOMP
#include "wx/wx.h"
#endif

#include <wx/taskbar.h>
#include <wx/timer.h>
#include <wx/listctrl.h>
#include <wx/spinctrl.h>
#include <wx/fileconf.h>
#include <wx/stdpaths.h>
#include <wx/filename.h>
#include <wx/datetime.h>
#include <wx/checkbox.h>
#include <wx/display.h>
#include <wx/statline.h>
#include <wx/imaglist.h>
#include <wx/wizard.h>
#include <wx/checklst.h>

#include <windows.h>
#include <psapi.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <vector>
#include <string>
#include <map>
#include <set>
#include <algorithm>

#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "shell32.lib")

 // -----------------------------------------
 // Colors
 // -----------------------------------------
#define CLR_BG      wxColour(26,  26,  46 )
#define CLR_PANEL   wxColour(22,  33,  62 )
#define CLR_RED     wxColour(233, 69,  96 )
#define CLR_BLUE    wxColour(15,  52,  96 )
#define CLR_TEXT    wxColour(192, 192, 224)
#define CLR_DIM     wxColour(85,  85,  119)
#define CLR_GREEN   wxColour(68,  255, 136)
#define CLR_ORANGE  wxColour(255, 170, 0  )

// -----------------------------------------
// Structs
// -----------------------------------------
struct ProcessInfo {
    wxString exeName;
    wxString exePath;
    DWORD    pid;
    wxIcon   icon;
    bool     hasIcon = false;
};

struct WorkApp {
    wxString exeName;   // "Code.exe"
    wxString label;     // "Visual Studio Code"
};

struct Session {
    wxString appName;
    int      duration;
    wxString date;
    wxString endTime;
};

struct AppConfig {
    std::vector<WorkApp> workApps;
    bool     colorAlert = true;
    int      alertMinutes = 30;
    bool     alwaysOnTop = true;
    bool     startInTray = false;
    bool     onboardDone = false;
    int      todayTotal = 0;
    wxString lastDate;
};

// -----------------------------------------
// Win32 helpers
// -----------------------------------------
std::vector<ProcessInfo> GetRunningProcesses() {
    std::vector<ProcessInfo> result;
    std::set<wxString> seen;

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return result;

    PROCESSENTRY32W pe; pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
            wxString exeName(pe.szExeFile);
            if (exeName.IsEmpty() || seen.count(exeName)) continue;
            seen.insert(exeName);

            ProcessInfo info;
            info.exeName = exeName;
            info.pid = pe.th32ProcessID;

            HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
            if (hProc) {
                wchar_t path[MAX_PATH]; DWORD sz = MAX_PATH;
                if (QueryFullProcessImageNameW(hProc, 0, path, &sz))
                    info.exePath = wxString(path);
                CloseHandle(hProc);
            }

            if (!info.exePath.IsEmpty()) {
                HICON hIco = NULL;
                ExtractIconExW(info.exePath.wc_str(), 0, NULL, &hIco, 1);
                if (!hIco) ExtractIconExW(info.exePath.wc_str(), 0, &hIco, NULL, 1);
                if (hIco) {
                    info.icon.CreateFromHICON(hIco);
                    info.hasIcon = info.icon.IsOk();
                    DestroyIcon(hIco);
                }
            }
            result.push_back(info);
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);

    std::sort(result.begin(), result.end(),
        [](const ProcessInfo& a, const ProcessInfo& b) {
            return a.exeName.CmpNoCase(b.exeName) < 0;
        });
    return result;
}

wxString GetForegroundProcessName() {
    HWND hwnd = GetForegroundWindow();
    if (!hwnd) return wxEmptyString;
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!hProc) return wxEmptyString;
    wchar_t buf[MAX_PATH]; DWORD sz = MAX_PATH;
    wxString name;
    if (QueryFullProcessImageNameW(hProc, 0, buf, &sz))
        name = wxFileName(wxString(buf)).GetFullName();
    CloseHandle(hProc);
    return name;
}

// -----------------------------------------
// Config
// -----------------------------------------
wxString GetConfigPath() {
    wxFileName fn(wxStandardPaths::Get().GetUserDataDir(), "work_timer.ini");
    fn.Mkdir(wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
    return fn.GetFullPath();
}

void SaveConfig(const AppConfig& cfg, const std::vector<Session>& sessions) {
    wxFileConfig fc(wxEmptyString, wxEmptyString, GetConfigPath());
    fc.Write("/settings/colorAlert", cfg.colorAlert);
    fc.Write("/settings/alertMinutes", cfg.alertMinutes);
    fc.Write("/settings/alwaysOnTop", cfg.alwaysOnTop);
    fc.Write("/settings/startInTray", cfg.startInTray);
    fc.Write("/settings/onboardDone", cfg.onboardDone);
    fc.Write("/settings/todayTotal", cfg.todayTotal);
    fc.Write("/settings/lastDate", cfg.lastDate);

    fc.DeleteGroup("/apps");
    for (int i = 0; i < (int)cfg.workApps.size(); i++) {
        fc.Write(wxString::Format("/apps/exe%d", i), cfg.workApps[i].exeName);
        fc.Write(wxString::Format("/apps/label%d", i), cfg.workApps[i].label);
    }

    fc.DeleteGroup("/sessions");
    int keep = std::max(0, (int)sessions.size() - 500);
    for (int i = keep; i < (int)sessions.size(); i++) {
        int idx = i - keep;
        fc.Write(wxString::Format("/sessions/s%d_app", idx), sessions[i].appName);
        fc.Write(wxString::Format("/sessions/s%d_dur", idx), sessions[i].duration);
        fc.Write(wxString::Format("/sessions/s%d_date", idx), sessions[i].date);
        fc.Write(wxString::Format("/sessions/s%d_end", idx), sessions[i].endTime);
    }
    fc.Write("/sessions/count", (int)(sessions.size() - keep));
    fc.Flush();
}

AppConfig LoadConfig(std::vector<Session>& sessions) {
    AppConfig cfg;
    wxFileConfig fc(wxEmptyString, wxEmptyString, GetConfigPath());
    cfg.colorAlert = fc.ReadBool("/settings/colorAlert", true);
    cfg.alertMinutes = fc.ReadLong("/settings/alertMinutes", 30);
    cfg.alwaysOnTop = fc.ReadBool("/settings/alwaysOnTop", true);
    cfg.startInTray = fc.ReadBool("/settings/startInTray", false);
    cfg.onboardDone = fc.ReadBool("/settings/onboardDone", false);
    cfg.todayTotal = fc.ReadLong("/settings/todayTotal", 0);
    cfg.lastDate = fc.Read("/settings/lastDate", wxEmptyString);

    for (int i = 0; ; i++) {
        wxString ke = wxString::Format("/apps/exe%d", i);
        if (!fc.HasEntry(ke)) break;
        WorkApp a;
        a.exeName = fc.Read(ke, wxEmptyString);
        a.label = fc.Read(wxString::Format("/apps/label%d", i), a.exeName);
        if (!a.exeName.IsEmpty()) cfg.workApps.push_back(a);
    }

    int cnt = fc.ReadLong("/sessions/count", 0);
    for (int i = 0; i < cnt; i++) {
        Session s;
        s.appName = fc.Read(wxString::Format("/sessions/s%d_app", i), "");
        s.duration = fc.ReadLong(wxString::Format("/sessions/s%d_dur", i), 0);
        s.date = fc.Read(wxString::Format("/sessions/s%d_date", i), "");
        s.endTime = fc.Read(wxString::Format("/sessions/s%d_end", i), "");
        if (!s.appName.IsEmpty()) sessions.push_back(s);
    }
    return cfg;
}

wxString FormatTime(int secs) {
    return wxString::Format("%02d:%02d:%02d",
        secs / 3600, (secs % 3600) / 60, secs % 60);
}

// =========================================
// Forward declarations
// =========================================
class MainFrame;

// =========================================
// Tray Icon
// =========================================
class TrayIcon : public wxTaskBarIcon {
public:
    MainFrame* m_frame;
    explicit TrayIcon(MainFrame* f) : m_frame(f) {}
    wxMenu* CreatePopupMenu() override;
    void OnLeftDClick(wxTaskBarIconEvent&);
    wxDECLARE_EVENT_TABLE();
};

// =========================================
// Add App Dialog
// =========================================
class AddAppDialog : public wxDialog {
public:
    WorkApp result;

    explicit AddAppDialog(wxWindow* parent)
        : wxDialog(parent, wxID_ANY, "Add Work App",
            wxDefaultPosition, wxSize(480, 520),
            wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
    {
        SetBackgroundColour(CLR_BG);
        auto* main = new wxBoxSizer(wxVERTICAL);

        auto* lbl = new wxStaticText(this, wxID_ANY,
            "Select a running process, or type an .exe name:");
        lbl->SetForegroundColour(CLR_TEXT);
        main->Add(lbl, 0, wxLEFT | wxTOP | wxRIGHT, 12);

        auto* row = new wxBoxSizer(wxHORIZONTAL);
        m_entry = new wxTextCtrl(this, wxID_ANY, wxEmptyString,
            wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
        m_entry->SetBackgroundColour(CLR_PANEL);
        m_entry->SetForegroundColour(*wxWHITE);
        row->Add(m_entry, 1, wxEXPAND | wxRIGHT, 6);

        auto* addBtn = new wxButton(this, wxID_ANY, "Add");
        addBtn->SetBackgroundColour(CLR_RED);
        addBtn->SetForegroundColour(*wxWHITE);
        row->Add(addBtn, 0);
        main->Add(row, 0, wxEXPAND | wxALL, 12);

        auto* lbl2 = new wxStaticText(this, wxID_ANY,
            "Running processes (double-click to select):");
        lbl2->SetForegroundColour(CLR_DIM);
        main->Add(lbl2, 0, wxLEFT | wxBOTTOM, 4);

        // Search bar
        auto* searchRow = new wxBoxSizer(wxHORIZONTAL);
        auto* searchLbl = new wxStaticText(this, wxID_ANY, "Search:");
        searchLbl->SetForegroundColour(CLR_TEXT);
        searchRow->Add(searchLbl, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
        m_search = new wxTextCtrl(this, wxID_ANY);
        m_search->SetBackgroundColour(CLR_PANEL);
        m_search->SetForegroundColour(*wxWHITE);
        searchRow->Add(m_search, 1);
        main->Add(searchRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

        m_imgList = new wxImageList(16, 16, true);
        m_list = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
            wxLC_REPORT | wxLC_SINGLE_SEL | wxBORDER_NONE);
        m_list->SetBackgroundColour(CLR_PANEL);
        m_list->SetForegroundColour(*wxWHITE);
        m_list->SetTextColour(*wxWHITE);
        m_list->SetImageList(m_imgList, wxIMAGE_LIST_SMALL);
        m_list->InsertColumn(0, "Process", wxLIST_FORMAT_LEFT, 180);
        m_list->InsertColumn(1, "Path", wxLIST_FORMAT_LEFT, 260);
        main->Add(m_list, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 12);

        SetSizer(main);

        // Fill process list
        m_procs = GetRunningProcesses();
        RebuildList(wxEmptyString);

        // Search filter
        m_search->Bind(wxEVT_TEXT, [this](wxCommandEvent&) {
            RebuildList(m_search->GetValue().Lower());
            });

        m_list->Bind(wxEVT_LIST_ITEM_SELECTED, [this](wxListEvent& e) {
            long itemIdx = e.GetIndex();
            if (itemIdx >= 0 && itemIdx < m_list->GetItemCount()) {
                wxListItem item;
                item.SetId(itemIdx);
                item.SetColumn(0);
                item.SetMask(wxLIST_MASK_TEXT);
                m_list->GetItem(item);
                m_entry->SetValue(item.GetText());
            }
            });
        m_list->Bind(wxEVT_LIST_ITEM_ACTIVATED, [this](wxListEvent&) { OnAdd(); });
        addBtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { OnAdd(); });
        m_entry->Bind(wxEVT_TEXT_ENTER, [this](wxCommandEvent&) { OnAdd(); });
    }

    ~AddAppDialog() override {}

private:
    wxTextCtrl* m_entry;
    wxTextCtrl* m_search;
    wxListCtrl* m_list;
    wxImageList* m_imgList;
    std::vector<ProcessInfo> m_procs;
    std::map<wxString, int>  m_iconIndexCache;

    void RebuildList(const wxString& filter) {
        m_list->DeleteAllItems();

        for (auto& p : m_procs) {
            if (!filter.IsEmpty() && !p.exeName.Lower().Contains(filter)) continue;

            int imgIdx = -1;
            auto it = m_iconIndexCache.find(p.exeName);
            if (it != m_iconIndexCache.end()) {
                imgIdx = it->second;
            }
            else if (p.hasIcon && p.icon.IsOk()) {
                wxBitmap bmp(p.icon);
                if (bmp.IsOk()) {
                    wxImage img = bmp.ConvertToImage().Rescale(16, 16);
                    imgIdx = m_imgList->Add(wxBitmap(img));
                }
                m_iconIndexCache[p.exeName] = imgIdx;
            }

            long idx = m_list->InsertItem(m_list->GetItemCount(), p.exeName, imgIdx);
            m_list->SetItem(idx, 1, p.exePath);
        }
    }

    void OnAdd() {
        wxString exe = m_entry->GetValue().Trim();
        if (exe.IsEmpty()) return;
        if (!exe.Lower().EndsWith(".exe")) exe += ".exe";
        result.exeName = exe;
        result.label = wxFileName(exe).GetName();
        EndModal(wxID_OK);
    }
};

// =========================================
// Onboarding Wizard
// =========================================
class OnboardWizard : public wxWizard {
public:
    std::vector<WorkApp> selectedApps;

    explicit OnboardWizard(wxWindow* parent)
        : wxWizard(parent, wxID_ANY, "WorkTimer - First Run Setup",
            wxNullBitmap, wxDefaultPosition, wxDEFAULT_DIALOG_STYLE)
    {
        SetBackgroundColour(CLR_BG);

        // Page 1: Welcome
        m_p1 = new wxWizardPageSimple(this);
        m_p1->SetBackgroundColour(CLR_BG);
        {
            auto* s = new wxBoxSizer(wxVERTICAL);
            auto* t = new wxStaticText(m_p1, wxID_ANY, "Welcome to WorkTimer");
            t->SetForegroundColour(CLR_RED);
            wxFont f = t->GetFont(); f.SetPointSize(15); f.SetWeight(wxFONTWEIGHT_BOLD);
            t->SetFont(f);
            s->Add(t, 0, wxALL, 14);
            auto* d = new wxStaticText(m_p1, wxID_ANY,
                "WorkTimer tracks how long you work in specific apps.\n\n"
                "It watches which app is in focus and automatically\n"
                "starts and stops the timer for you.\n\n"
                "This setup takes about 30 seconds.\n"
                "Click Next to choose your work apps.");
            d->SetForegroundColour(CLR_TEXT);
            s->Add(d, 0, wxLEFT, 14);
            m_p1->SetSizer(s);
        }

        // Page 2: Pick apps
        m_p2 = new wxWizardPageSimple(this, m_p1);
        m_p2->SetBackgroundColour(CLR_BG);
        {
            auto* s = new wxBoxSizer(wxVERTICAL);
            auto* t = new wxStaticText(m_p2, wxID_ANY, "Choose apps to track");
            t->SetForegroundColour(CLR_RED);
            wxFont f = t->GetFont(); f.SetWeight(wxFONTWEIGHT_BOLD); f.SetPointSize(11);
            t->SetFont(f);
            s->Add(t, 0, wxALL, 12);
            auto* h = new wxStaticText(m_p2, wxID_ANY,
                "Check apps you work in. Timer starts when that app is active.");
            h->SetForegroundColour(CLR_DIM);
            s->Add(h, 0, wxLEFT | wxBOTTOM, 8);

            // Search box
            auto* searchRow = new wxBoxSizer(wxHORIZONTAL);
            auto* searchLbl = new wxStaticText(m_p2, wxID_ANY, "Search:");
            searchLbl->SetForegroundColour(CLR_TEXT);
            searchRow->Add(searchLbl, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
            m_search = new wxTextCtrl(m_p2, wxID_ANY);
            m_search->SetBackgroundColour(CLR_PANEL);
            m_search->SetForegroundColour(*wxWHITE);
            searchRow->Add(m_search, 1);
            s->Add(searchRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);

            m_checkList = new wxCheckListBox(m_p2, wxID_ANY);
            m_checkList->SetBackgroundColour(CLR_PANEL);
            m_checkList->SetForegroundColour(*wxWHITE);
            s->Add(m_checkList, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 12);

            auto procs = GetRunningProcesses();
            for (auto& p : procs) {
                if (p.exeName == "WorkTimer.exe" || p.exeName == "explorer.exe" ||
                    p.exeName == "svchost.exe" || p.exeName == "System") continue;
                m_procs.push_back(p);
                m_checkList->Append(p.exeName);
            }

            // Live search filter
            m_search->Bind(wxEVT_TEXT, [this](wxCommandEvent&) {
                wxString filter = m_search->GetValue().Lower();
                std::set<wxString> checked;
                for (unsigned i = 0; i < m_checkList->GetCount(); i++)
                    if (m_checkList->IsChecked(i))
                        checked.insert(m_checkList->GetString(i));
                m_checkList->Clear();
                for (auto& p : m_procs) {
                    if (filter.IsEmpty() || p.exeName.Lower().Contains(filter)) {
                        unsigned idx = m_checkList->GetCount();
                        m_checkList->Append(p.exeName);
                        if (checked.count(p.exeName))
                            m_checkList->Check(idx, true);
                    }
                }
                });

            m_p2->SetSizer(s);
        }

        // Page 3: Options
        m_p3 = new wxWizardPageSimple(this, m_p2);
        m_p3->SetBackgroundColour(CLR_BG);
        {
            auto* s = new wxBoxSizer(wxVERTICAL);
            auto* t = new wxStaticText(m_p3, wxID_ANY, "Preferences");
            t->SetForegroundColour(CLR_RED);
            wxFont f = t->GetFont(); f.SetWeight(wxFONTWEIGHT_BOLD); f.SetPointSize(11);
            t->SetFont(f);
            s->Add(t, 0, wxALL, 12);

            m_cbTop = new wxCheckBox(m_p3, wxID_ANY, "Always on top (recommended)");
            m_cbTop->SetValue(true);
            m_cbTop->SetForegroundColour(CLR_TEXT); m_cbTop->SetBackgroundColour(CLR_BG);
            s->Add(m_cbTop, 0, wxLEFT | wxBOTTOM, 16);

            m_cbTray = new wxCheckBox(m_p3, wxID_ANY, "Start minimized to system tray");
            m_cbTray->SetValue(false);
            m_cbTray->SetForegroundColour(CLR_TEXT); m_cbTray->SetBackgroundColour(CLR_BG);
            s->Add(m_cbTray, 0, wxLEFT | wxBOTTOM, 16);

            m_cbAlert = new wxCheckBox(m_p3, wxID_ANY, "Flash color alert every 30 min");
            m_cbAlert->SetValue(true);
            m_cbAlert->SetForegroundColour(CLR_TEXT); m_cbAlert->SetBackgroundColour(CLR_BG);
            s->Add(m_cbAlert, 0, wxLEFT | wxBOTTOM, 16);

            auto* done = new wxStaticText(m_p3, wxID_ANY,
                "Click Finish to start WorkTimer.");
            done->SetForegroundColour(CLR_DIM);
            s->Add(done, 0, wxLEFT | wxTOP, 12);
            m_p3->SetSizer(s);
        }

        wxWizardPageSimple::Chain(m_p1, m_p2);
        wxWizardPageSimple::Chain(m_p2, m_p3);
        GetPageAreaSizer()->Add(m_p1);
        SetSize(wxSize(500, 440));
    }

    wxWizardPage* GetFirstPage() const { return m_p1; }
    bool startInTray() const { return m_cbTray->GetValue(); }
    bool alwaysOnTop() const { return m_cbTop->GetValue(); }
    bool colorAlert()  const { return m_cbAlert->GetValue(); }

    void CollectApps() {
        selectedApps.clear();
        // Collect checked names from currently visible list
        std::set<wxString> checkedNames;
        for (unsigned i = 0; i < m_checkList->GetCount(); i++)
            if (m_checkList->IsChecked(i))
                checkedNames.insert(m_checkList->GetString(i));
        // Map back to full proc info
        for (auto& p : m_procs) {
            if (checkedNames.count(p.exeName)) {
                WorkApp a;
                a.exeName = p.exeName;
                a.label = wxFileName(p.exeName).GetName();
                selectedApps.push_back(a);
            }
        }
    }

private:
    wxWizardPageSimple* m_p1, * m_p2, * m_p3;
    wxCheckListBox* m_checkList;
    wxTextCtrl* m_search;
    wxCheckBox* m_cbTray, * m_cbTop, * m_cbAlert;
    std::vector<ProcessInfo> m_procs;
};

// =========================================
// Main Frame
// =========================================
class MainFrame : public wxFrame {
public:
    MainFrame();
    ~MainFrame() override;

private:
    wxStaticText* m_timerLabel;
    wxStaticText* m_statusLabel;
    wxStaticText* m_todayLabel;
    wxButton* m_startBtn;
    wxListCtrl* m_appList;
    wxImageList* m_appImgList;
    TrayIcon* m_tray;

    wxTimer m_ticker;
    wxTimer m_monitor;

    AppConfig            m_cfg;
    std::vector<Session> m_sessions;
    int      m_elapsed = 0;
    bool     m_running = false;
    wxString m_curApp;
    std::map<wxString, int> m_iconCache;

    void BuildUI();
    void UpdateDisplay();
    void UpdateTodayLabel();
    void StartTimer(const wxString& appName = wxEmptyString);
    void StopTimer();
    void ResetTimer();
    void RefreshAppList();
    int  GetOrLoadIcon(const wxString& exeName);

    void OnTick(wxTimerEvent&);
    void OnMonitor(wxTimerEvent&);
    void OnToggle(wxCommandEvent&);
    void OnReset(wxCommandEvent&);
    void OnAddApp(wxCommandEvent&);
    void OnRemoveApp(wxCommandEvent&);
    void OnSettings(wxCommandEvent&);
    void OnIconize(wxIconizeEvent&);
    void OnClose(wxCloseEvent&);

    wxDECLARE_EVENT_TABLE();
};

enum {
    ID_TICK = wxID_HIGHEST + 1,
    ID_MONITOR,
    ID_TOGGLE,
    ID_RESET,
    ID_ADD_APP,
    ID_SETTINGS,
    ID_TRAY_SHOW,
    ID_TRAY_QUIT,
};

wxBEGIN_EVENT_TABLE(MainFrame, wxFrame)
EVT_TIMER(ID_TICK, MainFrame::OnTick)
EVT_TIMER(ID_MONITOR, MainFrame::OnMonitor)
EVT_BUTTON(ID_TOGGLE, MainFrame::OnToggle)
EVT_BUTTON(ID_RESET, MainFrame::OnReset)
EVT_BUTTON(ID_ADD_APP, MainFrame::OnAddApp)
EVT_BUTTON(ID_SETTINGS, MainFrame::OnSettings)
EVT_ICONIZE(MainFrame::OnIconize)
EVT_CLOSE(MainFrame::OnClose)
wxEND_EVENT_TABLE()

// -----------------------------------------
// TrayIcon
// -----------------------------------------
wxBEGIN_EVENT_TABLE(TrayIcon, wxTaskBarIcon)
EVT_TASKBAR_LEFT_DCLICK(TrayIcon::OnLeftDClick)
wxEND_EVENT_TABLE()

void TrayIcon::OnLeftDClick(wxTaskBarIconEvent&) {
    if (m_frame) { m_frame->Show(true); m_frame->Restore(); m_frame->Raise(); }
}

wxMenu* TrayIcon::CreatePopupMenu() {
    auto* menu = new wxMenu();
    menu->Append(ID_TRAY_SHOW, "Show WorkTimer");
    menu->AppendSeparator();
    menu->Append(ID_TRAY_QUIT, "Quit");
    menu->Bind(wxEVT_MENU, [this](wxCommandEvent&) {
        m_frame->Show(true); m_frame->Restore(); m_frame->Raise();
        }, ID_TRAY_SHOW);
    menu->Bind(wxEVT_MENU, [this](wxCommandEvent&) {
        m_frame->Destroy();
        }, ID_TRAY_QUIT);
    return menu;
}

// -----------------------------------------
// MainFrame
// -----------------------------------------
MainFrame::MainFrame()
    : wxFrame(nullptr, wxID_ANY, "Work Timer",
        wxDefaultPosition, wxSize(340, 470),
        wxDEFAULT_FRAME_STYLE & ~(wxRESIZE_BORDER | wxMAXIMIZE_BOX)),
    m_ticker(this, ID_TICK),
    m_monitor(this, ID_MONITOR),
    m_tray(nullptr)
{
    m_cfg = LoadConfig(m_sessions);

    wxString today = wxDateTime::Now().FormatISODate();
    if (m_cfg.lastDate != today) {
        m_cfg.todayTotal = 0;
        m_cfg.lastDate = today;
    }

    // Onboarding
    if (!m_cfg.onboardDone) {
        OnboardWizard wiz(this);
        if (wiz.RunWizard(wiz.GetFirstPage())) {
            wiz.CollectApps();
            m_cfg.workApps = wiz.selectedApps;
            m_cfg.startInTray = wiz.startInTray();
            m_cfg.alwaysOnTop = wiz.alwaysOnTop();
            m_cfg.colorAlert = wiz.colorAlert();
            m_cfg.onboardDone = true;
            SaveConfig(m_cfg, m_sessions);
        }
    }

    if (m_cfg.alwaysOnTop)
        SetWindowStyle(GetWindowStyle() | wxSTAY_ON_TOP);

    wxDisplay disp;
    wxRect area = disp.GetClientArea();
    SetPosition(wxPoint(area.GetRight() - GetSize().x - 20,
        area.GetBottom() - GetSize().y - 20));

    BuildUI();

    // Tray icon
    m_tray = new TrayIcon(this);
    wxIcon trayIco;
    if (wxFileExists("app.ico"))
        trayIco.LoadFile("app.ico", wxBITMAP_TYPE_ICO);
    if (!trayIco.IsOk())
        trayIco.LoadFile("app.ico", wxBITMAP_TYPE_ICO);
    m_tray->SetIcon(trayIco, "WorkTimer");

    Show(!m_cfg.startInTray);

    m_ticker.Start(1000);
    m_monitor.Start(1000);
}

MainFrame::~MainFrame() {
    m_ticker.Stop();
    m_monitor.Stop();
    if (m_tray) { m_tray->RemoveIcon(); delete m_tray; m_tray = nullptr; }
}

void MainFrame::BuildUI() {
    SetBackgroundColour(CLR_BG);
    auto* root = new wxBoxSizer(wxVERTICAL);

    // Header
    auto* hdr = new wxPanel(this, wxID_ANY);
    hdr->SetBackgroundColour(CLR_PANEL);
    auto* hRow = new wxBoxSizer(wxHORIZONTAL);
    auto* titleLbl = new wxStaticText(hdr, wxID_ANY, "WORK TIMER");
    titleLbl->SetForegroundColour(CLR_RED);
    wxFont tf = titleLbl->GetFont();
    tf.SetPointSize(13); tf.SetWeight(wxFONTWEIGHT_BOLD); tf.SetFaceName("Consolas");
    titleLbl->SetFont(tf);
    hRow->Add(titleLbl, 1, wxALIGN_CENTER_VERTICAL | wxLEFT, 12);
    auto* settBtn = new wxButton(hdr, ID_SETTINGS, "\u2699", wxDefaultPosition, wxSize(32, 28));
    settBtn->SetBackgroundColour(CLR_PANEL);
    settBtn->SetForegroundColour(CLR_TEXT);
    hRow->Add(settBtn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 6);
    hdr->SetSizer(hRow);
    root->Add(hdr, 0, wxEXPAND);

    // Timer
    auto* tp = new wxPanel(this, wxID_ANY);
    tp->SetBackgroundColour(CLR_BG);
    auto* tCol = new wxBoxSizer(wxVERTICAL);
    m_timerLabel = new wxStaticText(tp, wxID_ANY, "00:00:00",
        wxDefaultPosition, wxDefaultSize, wxALIGN_CENTER);
    wxFont tf2 = m_timerLabel->GetFont();
    tf2.SetPointSize(34); tf2.SetWeight(wxFONTWEIGHT_BOLD); tf2.SetFaceName("Consolas");
    m_timerLabel->SetFont(tf2);
    m_timerLabel->SetForegroundColour(CLR_DIM);
    tCol->Add(m_timerLabel, 0, wxALIGN_CENTER | wxTOP, 10);
    m_statusLabel = new wxStaticText(tp, wxID_ANY, "\u25cf Idle",
        wxDefaultPosition, wxDefaultSize, wxALIGN_CENTER);
    m_statusLabel->SetForegroundColour(CLR_DIM);
    tCol->Add(m_statusLabel, 0, wxALIGN_CENTER | wxBOTTOM, 8);
    tp->SetSizer(tCol);
    root->Add(tp, 0, wxEXPAND);

    // Today bar
    auto* todayPnl = new wxPanel(this, wxID_ANY);
    todayPnl->SetBackgroundColour(CLR_PANEL);
    auto* todayRow = new wxBoxSizer(wxHORIZONTAL);
    auto* tl = new wxStaticText(todayPnl, wxID_ANY, "Today total");
    tl->SetForegroundColour(CLR_DIM);
    todayRow->Add(tl, 1, wxALIGN_CENTER_VERTICAL | wxLEFT, 12);
    m_todayLabel = new wxStaticText(todayPnl, wxID_ANY, "00:00:00");
    wxFont tf3 = m_todayLabel->GetFont();
    tf3.SetFaceName("Consolas"); tf3.SetWeight(wxFONTWEIGHT_BOLD);
    m_todayLabel->SetFont(tf3);
    m_todayLabel->SetForegroundColour(CLR_BLUE);
    todayRow->Add(m_todayLabel, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 12);
    todayPnl->SetSizer(todayRow);
    root->Add(todayPnl, 0, wxEXPAND);
    UpdateTodayLabel();

    // Buttons
    auto* btnRow = new wxBoxSizer(wxHORIZONTAL);
    m_startBtn = new wxButton(this, ID_TOGGLE, "\u25b6 Start");
    m_startBtn->SetBackgroundColour(CLR_RED);
    m_startBtn->SetForegroundColour(*wxWHITE);
    btnRow->Add(m_startBtn, 1, wxEXPAND | wxALL, 6);
    auto* resetBtn = new wxButton(this, ID_RESET, "\u21ba Reset");
    resetBtn->SetBackgroundColour(CLR_BLUE);
    resetBtn->SetForegroundColour(CLR_TEXT);
    btnRow->Add(resetBtn, 1, wxEXPAND | wxTOP | wxRIGHT | wxBOTTOM, 6);
    root->Add(btnRow, 0, wxEXPAND | wxLEFT | wxRIGHT, 6);

    root->Add(new wxStaticLine(this), 0, wxEXPAND | wxLEFT | wxRIGHT, 12);

    // App list header
    auto* lh = new wxBoxSizer(wxHORIZONTAL);
    auto* lt = new wxStaticText(this, wxID_ANY, "Work Apps");
    lt->SetForegroundColour(CLR_TEXT);
    wxFont lf = lt->GetFont(); lf.SetWeight(wxFONTWEIGHT_BOLD);
    lt->SetFont(lf);
    lh->Add(lt, 1, wxALIGN_CENTER_VERTICAL | wxLEFT, 12);
    auto* addBtn = new wxButton(this, ID_ADD_APP, "+ Add", wxDefaultPosition, wxSize(-1, 24));
    addBtn->SetBackgroundColour(CLR_BLUE);
    addBtn->SetForegroundColour(wxColour(112, 144, 255));
    lh->Add(addBtn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 12);
    root->Add(lh, 0, wxEXPAND | wxTOP | wxBOTTOM, 4);

    // App list ctrl
    m_appImgList = new wxImageList(16, 16, true);
    m_appList = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
        wxLC_REPORT | wxLC_SINGLE_SEL | wxBORDER_NONE);
    m_appList->SetBackgroundColour(CLR_PANEL);
    m_appList->SetForegroundColour(CLR_TEXT);
    m_appList->SetTextColour(CLR_TEXT);
    m_appList->SetImageList(m_appImgList, wxIMAGE_LIST_SMALL);
    m_appList->InsertColumn(0, "Name", wxLIST_FORMAT_LEFT, 140);
    m_appList->InsertColumn(1, "Process", wxLIST_FORMAT_LEFT, 160);
    root->Add(m_appList, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 12);

    m_appList->Bind(wxEVT_LIST_ITEM_ACTIVATED, [this](wxListEvent&) {
        wxCommandEvent d; OnRemoveApp(d);
        });
    m_appList->Bind(wxEVT_LIST_KEY_DOWN, [this](wxListEvent& e) {
        if (e.GetKeyCode() == WXK_DELETE) { wxCommandEvent d; OnRemoveApp(d); }
        else e.Skip();
        });

    auto* hint = new wxStaticText(this, wxID_ANY,
        "Double-click or DEL to remove  |  Active app auto-starts timer");
    hint->SetForegroundColour(CLR_DIM);
    wxFont hf = hint->GetFont(); hf.SetPointSize(7);
    hint->SetFont(hf);
    root->Add(hint, 0, wxALIGN_CENTER | wxBOTTOM, 4);

    SetSizer(root);
    Layout();
    RefreshAppList();
}

// -----------------------------------------
// Icon helper
// -----------------------------------------
int MainFrame::GetOrLoadIcon(const wxString& exeName) {
    auto it = m_iconCache.find(exeName);
    if (it != m_iconCache.end()) return it->second;

    wxString path;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe; pe.dwSize = sizeof(pe);
        if (Process32FirstW(snap, &pe)) {
            do {
                if (exeName.CmpNoCase(wxString(pe.szExeFile)) == 0) {
                    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,
                        FALSE, pe.th32ProcessID);
                    if (h) {
                        wchar_t buf[MAX_PATH]; DWORD sz = MAX_PATH;
                        if (QueryFullProcessImageNameW(h, 0, buf, &sz))
                            path = wxString(buf);
                        CloseHandle(h);
                    }
                    break;
                }
            } while (Process32NextW(snap, &pe));
        }
        CloseHandle(snap);
    }

    int idx = -1;
    if (!path.IsEmpty()) {
        HICON hIco = NULL;
        ExtractIconExW(path.wc_str(), 0, NULL, &hIco, 1);
        if (!hIco) ExtractIconExW(path.wc_str(), 0, &hIco, NULL, 1);
        if (hIco) {
            wxIcon ico; ico.CreateFromHICON(hIco);
            wxBitmap bmp(ico);
            if (bmp.IsOk()) {
                wxImage img = bmp.ConvertToImage().Rescale(16, 16);
                idx = m_appImgList->Add(wxBitmap(img));
            }
            DestroyIcon(hIco);
        }
    }
    m_iconCache[exeName] = idx;
    return idx;
}

void MainFrame::RefreshAppList() {
    m_appList->DeleteAllItems();
    for (auto& a : m_cfg.workApps) {
        int imgIdx = GetOrLoadIcon(a.exeName);
        long idx = m_appList->InsertItem(m_appList->GetItemCount(), a.label, imgIdx);
        m_appList->SetItem(idx, 1, a.exeName);
    }
}

// -----------------------------------------
// Timer
// -----------------------------------------
void MainFrame::OnTick(wxTimerEvent&) {
    if (!m_running) return;
    m_elapsed++;
    m_cfg.todayTotal++;
    UpdateDisplay();
    if (m_cfg.colorAlert && m_elapsed > 0 &&
        m_elapsed % (m_cfg.alertMinutes * 60) == 0) {
        m_timerLabel->SetForegroundColour(CLR_ORANGE);
        wxBell();
    }
}

void MainFrame::OnMonitor(wxTimerEvent&) {
    wxString active = GetForegroundProcessName();
    if (active.IsEmpty()) return;

    wxString matched;
    for (auto& a : m_cfg.workApps) {
        if (active.CmpNoCase(a.exeName) == 0) { matched = a.exeName; break; }
    }

    if (!matched.IsEmpty() && !m_running)      StartTimer(matched);
    else if (matched.IsEmpty() && m_running && !m_curApp.IsEmpty()) StopTimer();
}

void MainFrame::StartTimer(const wxString& appName) {
    m_running = true;
    m_curApp = appName;
    m_startBtn->SetLabel("\u23f8 Stop");
    m_timerLabel->SetForegroundColour(CLR_RED);
    m_statusLabel->SetForegroundColour(CLR_GREEN);
    wxString label = appName.IsEmpty() ? "Manual" : appName;
    m_statusLabel->SetLabel("\u25cf Working: " + label);
}

void MainFrame::StopTimer() {
    m_running = false;
    if (m_elapsed > 0) {
        Session s;
        s.appName = m_curApp.IsEmpty() ? "Manual" : m_curApp;
        s.duration = m_elapsed;
        s.date = wxDateTime::Now().FormatISODate();
        s.endTime = wxDateTime::Now().Format("%H:%M");
        m_sessions.push_back(s);
        SaveConfig(m_cfg, m_sessions);
    }
    m_startBtn->SetLabel("\u25b6 Start");
    m_timerLabel->SetForegroundColour(wxColour(136, 102, 68));
    m_statusLabel->SetForegroundColour(CLR_ORANGE);
    m_statusLabel->SetLabel("\u25cf Paused");
}

void MainFrame::ResetTimer() {
    if (m_running) StopTimer();
    m_elapsed = 0;
    m_timerLabel->SetLabel("00:00:00");
    m_timerLabel->SetForegroundColour(CLR_DIM);
    m_statusLabel->SetLabel("\u25cf Idle");
    m_statusLabel->SetForegroundColour(CLR_DIM);
    m_startBtn->SetLabel("\u25b6 Start");
}

void MainFrame::UpdateDisplay() {
    m_timerLabel->SetLabel(FormatTime(m_elapsed));
    if (m_running) m_timerLabel->SetForegroundColour(CLR_RED);
    UpdateTodayLabel();
}

void MainFrame::UpdateTodayLabel() {
    m_todayLabel->SetLabel(FormatTime(m_cfg.todayTotal));
    m_todayLabel->SetForegroundColour(
        m_cfg.todayTotal > 0 ? wxColour(68, 136, 255) : CLR_BLUE);
}

// -----------------------------------------
// Event handlers
// -----------------------------------------
void MainFrame::OnToggle(wxCommandEvent&) {
    if (m_running) StopTimer(); else StartTimer();
}
void MainFrame::OnReset(wxCommandEvent&) { ResetTimer(); }

void MainFrame::OnAddApp(wxCommandEvent&) {
    AddAppDialog dlg(this);
    if (dlg.ShowModal() == wxID_OK && !dlg.result.exeName.IsEmpty()) {
        for (auto& a : m_cfg.workApps) {
            if (a.exeName.CmpNoCase(dlg.result.exeName) == 0) {
                wxMessageBox("Already registered.", "Info"); return;
            }
        }
        m_cfg.workApps.push_back(dlg.result);
        SaveConfig(m_cfg, m_sessions);
        RefreshAppList();
    }
}

void MainFrame::OnRemoveApp(wxCommandEvent&) {
    long sel = m_appList->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
    if (sel == wxNOT_FOUND) return;
    wxString name = m_cfg.workApps[sel].exeName;
    if (wxMessageBox("Remove '" + name + "'?", "Confirm",
        wxYES_NO | wxICON_QUESTION) == wxYES) {
        m_cfg.workApps.erase(m_cfg.workApps.begin() + sel);
        SaveConfig(m_cfg, m_sessions);
        RefreshAppList();
    }
}

void MainFrame::OnSettings(wxCommandEvent&) {
    wxDialog dlg(this, wxID_ANY, "Settings", wxDefaultPosition, wxSize(300, 380));
    dlg.SetBackgroundColour(CLR_BG);
    auto* s = new wxBoxSizer(wxVERTICAL);

    auto* t = new wxStaticText(&dlg, wxID_ANY, "Settings");
    t->SetForegroundColour(CLR_RED);
    wxFont tf = t->GetFont(); tf.SetPointSize(12); tf.SetWeight(wxFONTWEIGHT_BOLD);
    t->SetFont(tf);
    s->Add(t, 0, wxALIGN_CENTER | wxTOP | wxBOTTOM, 10);

    auto* cbAlert = new wxCheckBox(&dlg, wxID_ANY, "Color alert");
    cbAlert->SetValue(m_cfg.colorAlert);
    cbAlert->SetForegroundColour(CLR_TEXT); cbAlert->SetBackgroundColour(CLR_BG);
    s->Add(cbAlert, 0, wxLEFT | wxBOTTOM, 16);

    auto* r2 = new wxBoxSizer(wxHORIZONTAL);
    auto* l2 = new wxStaticText(&dlg, wxID_ANY, "Alert interval (min):");
    l2->SetForegroundColour(CLR_TEXT);
    r2->Add(l2, 1, wxALIGN_CENTER_VERTICAL);
    auto* spin = new wxSpinCtrl(&dlg, wxID_ANY, wxEmptyString,
        wxDefaultPosition, wxSize(60, -1));
    spin->SetRange(1, 120); spin->SetValue(m_cfg.alertMinutes);
    spin->SetBackgroundColour(CLR_PANEL); spin->SetForegroundColour(*wxWHITE);
    r2->Add(spin, 0);
    s->Add(r2, 0, wxLEFT | wxRIGHT | wxBOTTOM, 16);

    auto* cbTop = new wxCheckBox(&dlg, wxID_ANY, "Always on top");
    cbTop->SetValue(m_cfg.alwaysOnTop);
    cbTop->SetForegroundColour(CLR_TEXT); cbTop->SetBackgroundColour(CLR_BG);
    s->Add(cbTop, 0, wxLEFT | wxBOTTOM, 16);

    auto* cbTray = new wxCheckBox(&dlg, wxID_ANY, "Start minimized to tray");
    cbTray->SetValue(m_cfg.startInTray);
    cbTray->SetForegroundColour(CLR_TEXT); cbTray->SetBackgroundColour(CLR_BG);
    s->Add(cbTray, 0, wxLEFT | wxBOTTOM, 16);

    s->Add(new wxStaticLine(&dlg), 0, wxEXPAND | wxLEFT | wxRIGHT, 12);

    // Today stats
    wxString today = wxDateTime::Now().FormatISODate();
    std::map<wxString, int> appTimes; int cnt = 0;
    for (auto& ss : m_sessions)
        if (ss.date == today) { appTimes[ss.appName] += ss.duration; cnt++; }
    wxString stat = wxString::Format("Today: %d sessions\n", cnt);
    for (auto& p : appTimes)
        stat += wxString::Format("  %s: %s\n", p.first.Left(18), FormatTime(p.second));
    auto* statLbl = new wxStaticText(&dlg, wxID_ANY, stat);
    statLbl->SetForegroundColour(CLR_DIM);
    s->Add(statLbl, 0, wxLEFT | wxTOP, 12);

    s->AddStretchSpacer();
    auto* ok = new wxButton(&dlg, wxID_OK, "Apply");
    ok->SetBackgroundColour(CLR_RED); ok->SetForegroundColour(*wxWHITE);
    s->Add(ok, 0, wxALIGN_CENTER | wxBOTTOM, 12);
    dlg.SetSizer(s);

    if (dlg.ShowModal() == wxID_OK) {
        m_cfg.colorAlert = cbAlert->GetValue();
        m_cfg.alertMinutes = spin->GetValue();
        m_cfg.alwaysOnTop = cbTop->GetValue();
        m_cfg.startInTray = cbTray->GetValue();
        long style = GetWindowStyle();
        if (m_cfg.alwaysOnTop) style |= wxSTAY_ON_TOP;
        else                    style &= ~wxSTAY_ON_TOP;
        SetWindowStyle(style);
        SaveConfig(m_cfg, m_sessions);
    }
}

void MainFrame::OnIconize(wxIconizeEvent& e) {
    if (e.IsIconized()) Show(false);
}

void MainFrame::OnClose(wxCloseEvent&) {
    if (m_running) StopTimer();
    SaveConfig(m_cfg, m_sessions);
    Destroy();
}

// =========================================
// App entry
// =========================================
class WorkTimerApp : public wxApp {
public:
    bool OnInit() override {
        SetAppName("WorkTimer");
        wxInitAllImageHandlers();
        auto* frame = new MainFrame();
        frame->Show(true);
        return true;
    }
};

wxIMPLEMENT_APP(WorkTimerApp);