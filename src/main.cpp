/*
 * WorkTimer - 작업 시간 타이머
 * wxWidgets + Win32 API
 * Build: Visual Studio 2019/2022 + wxWidgets 3.2
 */

#include "wx/wxprec.h"
#ifndef WX_PRECOMP
    #include "wx/wx.h"
#endif

#include <wx/taskbar.h>
#include <wx/timer.h>
#include <wx/listctrl.h>
#include <wx/notebook.h>
#include <wx/spinctrl.h>
#include <wx/fileconf.h>
#include <wx/stdpaths.h>
#include <wx/filename.h>
#include <wx/datetime.h>
#include <wx/combobox.h>
#include <wx/checkbox.h>
#include <wx/display.h>
#include <wx/statline.h>

#include <windows.h>
#include <psapi.h>
#include <vector>
#include <string>
#include <map>

#pragma comment(lib, "psapi.lib")


// 전역 색상처리

#define CLR_BG          wxColour(26, 26, 46)
#define CLR_PANEL       wxColour(22, 33, 62)
#define CLR_RED         wxColour(233, 69, 96)
#define CLR_BLUE        wxColour(15, 52, 96)
#define CLR_TEXT        wxColour(192, 192, 224)
#define CLR_DIM         wxColour(85, 85, 119)
#define CLR_GREEN       wxColour(68, 255, 136)
#define CLR_ORANGE      wxColour(255, 170, 0)


// 컨피그 구조체

struct AppConfig {
    std::vector<wxString> workApps;   /작업 앱 키워드
    bool  colorAlert     = true;
    int   alertMinutes   = 30;
    bool  alwaysOnTop    = true;
    int   todayTotal     = 0;         // 오늘 총 작업 초
    wxString lastDate;
};

// 세션
struct Session {
    wxString appName;
    int      duration;   // seconds
    wxString date;
    wxString endTime;
};

// 실행중인 프로그램(창)
struct WindowInfo {
    wxString title;
    HWND     hwnd;
    DWORD    pid;
};

std::vector<WindowInfo> g_windows;

BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam) { // 전역 벡터에 창 정보 수집
    if (!IsWindowVisible(hwnd)) return TRUE;
    int len = GetWindowTextLengthW(hwnd);
    if (len <= 1) return TRUE;

    wchar_t buf[512];
    GetWindowTextW(hwnd, buf, 512);
    wxString title(buf);
    title.Trim();
    if (title.IsEmpty()) return TRUE;

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);

    g_windows.push_back({title, hwnd, pid});
    return TRUE;
}

std::vector<WindowInfo> GetRunningWindows() {
    g_windows.clear();
    EnumWindows(EnumWindowsProc, 0);
    return g_windows;
}

wxString GetActiveWindowTitle() {
    HWND hwnd = GetForegroundWindow();
    if (!hwnd) return wxEmptyString;
    wchar_t buf[512];
    GetWindowTextW(hwnd, buf, 512);
    return wxString(buf);
}

// 설정 파일 경로
wxString GetConfigPath() {
    wxFileName fn(wxStandardPaths::Get().GetUserDataDir(), "work_timer.ini");
    fn.Mkdir(wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
    return fn.GetFullPath();
}

void SaveConfig(const AppConfig& cfg, const std::vector<Session>& sessions) {
    wxFileConfig fc(wxEmptyString, wxEmptyString, GetConfigPath());

    fc.Write("/settings/colorAlert",   cfg.colorAlert);
    fc.Write("/settings/alertMinutes", cfg.alertMinutes);
    fc.Write("/settings/alwaysOnTop",  cfg.alwaysOnTop);
    fc.Write("/settings/todayTotal",   cfg.todayTotal);
    fc.Write("/settings/lastDate",     cfg.lastDate);

    fc.DeleteGroup("/apps");
    for (int i = 0; i < (int)cfg.workApps.size(); i++) {
        fc.Write(wxString::Format("/apps/app%d", i), cfg.workApps[i]);
    }

    fc.DeleteGroup("/sessions");
    int keep = std::max(0, (int)sessions.size() - 200);
    for (int i = keep; i < (int)sessions.size(); i++) {
        int idx = i - keep;
        auto& s = sessions[i];
        fc.Write(wxString::Format("/sessions/s%d_app",  idx), s.appName);
        fc.Write(wxString::Format("/sessions/s%d_dur",  idx), s.duration);
        fc.Write(wxString::Format("/sessions/s%d_date", idx), s.date);
        fc.Write(wxString::Format("/sessions/s%d_end",  idx), s.endTime);
    }
    fc.Write("/sessions/count", (int)(sessions.size() - keep));
    fc.Flush();
}

AppConfig LoadConfig(std::vector<Session>& sessions) { // 세션도 같이 로드
    AppConfig cfg;
    wxFileConfig fc(wxEmptyString, wxEmptyString, GetConfigPath());

    cfg.colorAlert   = fc.ReadBool("/settings/colorAlert",   true);
    cfg.alertMinutes = fc.ReadLong("/settings/alertMinutes", 30);
    cfg.alwaysOnTop  = fc.ReadBool("/settings/alwaysOnTop",  true);
    cfg.todayTotal   = fc.ReadLong("/settings/todayTotal",   0);
    cfg.lastDate     = fc.Read("/settings/lastDate", wxEmptyString);

    for (int i = 0; ; i++) {
        wxString key = wxString::Format("/apps/app%d", i);
        if (!fc.HasEntry(key)) break;
        cfg.workApps.push_back(fc.Read(key, wxEmptyString));
    }

    int cnt = fc.ReadLong("/sessions/count", 0);
    for (int i = 0; i < cnt; i++) {
        Session s;
        s.appName  = fc.Read(wxString::Format("/sessions/s%d_app",  i), "");
        s.duration = fc.ReadLong(wxString::Format("/sessions/s%d_dur", i), 0);
        s.date     = fc.Read(wxString::Format("/sessions/s%d_date", i), "");
        s.endTime  = fc.Read(wxString::Format("/sessions/s%d_end",  i), "");
        if (!s.appName.IsEmpty()) sessions.push_back(s);
    }
    return cfg;
}

wxString FormatTime(int secs) {
    int h = secs / 3600;
    int m = (secs % 3600) / 60;
    int s = secs % 60;
    return wxString::Format("%02d:%02d:%02d", h, m, s);
}

// 작업 앱 추가
class AddAppDialog : public wxDialog {
public:
    wxString result;

    AddAppDialog(wxWindow* parent)
        : wxDialog(parent, wxID_ANY, "작업 앱 추가", wxDefaultPosition,
                   wxSize(420, 480), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
    {
        SetBackgroundColour(CLR_BG);

        auto* main = new wxBoxSizer(wxVERTICAL);

        // 안내문
        auto* lbl = new wxStaticText(this, wxID_ANY, "실행 중인 창 선택 또는 키워드 직접 입력");
        lbl->SetForegroundColour(CLR_TEXT);
        main->Add(lbl, 0, wxLEFT|wxTOP, 12);

        // 입력 + 추가 버튼
        auto* row = new wxBoxSizer(wxHORIZONTAL);
        m_entry = new wxTextCtrl(this, wxID_ANY, wxEmptyString,
                                  wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
        m_entry->SetBackgroundColour(CLR_PANEL);
        m_entry->SetForegroundColour(*wxWHITE);
        row->Add(m_entry, 1, wxEXPAND|wxRIGHT, 6);

        auto* addBtn = new wxButton(this, wxID_ANY, "추가");
        addBtn->SetBackgroundColour(CLR_RED);
        addBtn->SetForegroundColour(*wxWHITE);
        row->Add(addBtn, 0);
        main->Add(row, 0, wxEXPAND|wxALL, 12);

        auto* lbl2 = new wxStaticText(this, wxID_ANY, "▼ 현재 실행 중인 창 (더블클릭하여 추가)");
        lbl2->SetForegroundColour(CLR_DIM);
        main->Add(lbl2, 0, wxLEFT|wxBOTTOM, 8);

        // 창 목록
        m_list = new wxListBox(this, wxID_ANY);
        m_list->SetBackgroundColour(CLR_PANEL);
        m_list->SetForegroundColour(CLR_TEXT);
        main->Add(m_list, 1, wxEXPAND|wxLEFT|wxRIGHT|wxBOTTOM, 12);

        auto* hint = new wxStaticText(this, wxID_ANY,
            "※ 창 제목의 일부 키워드를 등록하세요.\n"
            "   예) 'code' → Visual Studio Code 감지");
        hint->SetForegroundColour(CLR_DIM);
        main->Add(hint, 0, wxLEFT|wxBOTTOM, 12);

        SetSizer(main);

        // 창 목록 채우기
        auto wins = GetRunningWindows();
        for (auto& w : wins) {
            if (!w.title.IsEmpty())
                m_list->Append(w.title);
        }

        // 이벤트
        m_list->Bind(wxEVT_LISTBOX, [this](wxCommandEvent& e) {
            int sel = m_list->GetSelection();
            if (sel != wxNOT_FOUND) {
                wxString t = m_list->GetString(sel);
                if (t.Length() > 50) t = t.Left(50);
                m_entry->SetValue(t);
            }
        });
        m_list->Bind(wxEVT_LISTBOX_DCLICK, [this](wxCommandEvent&) { OnAdd(); });
        addBtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { OnAdd(); });
        m_entry->Bind(wxEVT_TEXT_ENTER, [this](wxCommandEvent&) { OnAdd(); });
    }

private:
    wxTextCtrl* m_entry;
    wxListBox*  m_list;

    void OnAdd() {
        result = m_entry->GetValue().Trim();
        if (!result.IsEmpty()) EndModal(wxID_OK);
    }
};

// 메인 프레임
class MainFrame : public wxFrame {
public:
    MainFrame();
    ~MainFrame();

private:
    // UI 요소
    wxStaticText*  m_timerLabel;
    wxStaticText*  m_statusLabel;
    wxStaticText*  m_todayLabel;
    wxButton*      m_startBtn;
    wxListBox*     m_appList;

    // 타이머
    wxTimer        m_ticker;
    wxTimer        m_monitor;

    // 상태
    AppConfig            m_cfg;
    std::vector<Session> m_sessions;
    int                  m_elapsed   = 0;
    bool                 m_running   = false;
    wxString             m_curApp;
    time_t               m_lastTick  = 0;

    // 메서드
    void BuildUI();
    void UpdateDisplay();
    void UpdateTodayLabel();
    void StartTimer(const wxString& appName = wxEmptyString);
    void StopTimer();
    void ResetTimer();
    void RefreshAppList();
    void CheckAlerts();

    // 이벤트 핸들러
    void OnTick(wxTimerEvent&);
    void OnMonitor(wxTimerEvent&);
    void OnToggle(wxCommandEvent&);
    void OnReset(wxCommandEvent&);
    void OnAddApp(wxCommandEvent&);
    void OnRemoveApp(wxCommandEvent&);
    void OnSettings(wxCommandEvent&);
    void OnClose(wxCloseEvent&);

    wxDECLARE_EVENT_TABLE();
};

enum {
    ID_TICK    = wxID_HIGHEST + 1,
    ID_MONITOR,
    ID_TOGGLE,
    ID_RESET,
    ID_ADD_APP,
    ID_SETTINGS,
};

wxBEGIN_EVENT_TABLE(MainFrame, wxFrame)
    EVT_TIMER(ID_TICK,    MainFrame::OnTick)
    EVT_TIMER(ID_MONITOR, MainFrame::OnMonitor)
    EVT_BUTTON(ID_TOGGLE,   MainFrame::OnToggle)
    EVT_BUTTON(ID_RESET,    MainFrame::OnReset)
    EVT_BUTTON(ID_ADD_APP,  MainFrame::OnAddApp)
    EVT_BUTTON(ID_SETTINGS, MainFrame::OnSettings)
    EVT_CLOSE(MainFrame::OnClose)
wxEND_EVENT_TABLE()

MainFrame::MainFrame()
    : wxFrame(nullptr, wxID_ANY, "Work Timer",
              wxDefaultPosition, wxSize(320, 430),
              wxDEFAULT_FRAME_STYLE & ~(wxRESIZE_BORDER | wxMAXIMIZE_BOX)),
      m_ticker(this, ID_TICK),
      m_monitor(this, ID_MONITOR)
{
    m_cfg = LoadConfig(m_sessions);

    // 날짜 초기화
    wxString today = wxDateTime::Now().FormatISODate();
    if (m_cfg.lastDate != today) {
        m_cfg.todayTotal = 0;
        m_cfg.lastDate   = today;
    }

    if (m_cfg.alwaysOnTop)
        SetWindowStyle(GetWindowStyle() | wxSTAY_ON_TOP);

    // 우측 하단 배치
    wxDisplay disp;
    wxRect area = disp.GetClientArea();
    SetPosition(wxPoint(area.GetRight() - GetSize().x - 20,
                        area.GetBottom() - GetSize().y - 20));

    BuildUI();

    m_ticker.Start(1000);
    m_monitor.Start(1000);
}

MainFrame::~MainFrame() {
    m_ticker.Stop();
    m_monitor.Stop();
}

void MainFrame::BuildUI() {
    SetBackgroundColour(CLR_BG);

    auto* root = new wxBoxSizer(wxVERTICAL);

    // ── 헤더 ──────────────────────────────
    auto* header = new wxPanel(this, wxID_ANY);
    header->SetBackgroundColour(CLR_PANEL);
    auto* hRow = new wxBoxSizer(wxHORIZONTAL);

    auto* title = new wxStaticText(header, wxID_ANY, "WORK TIMER");
    title->SetForegroundColour(CLR_RED);
    wxFont tf = title->GetFont();
    tf.SetPointSize(13); tf.SetWeight(wxFONTWEIGHT_BOLD);
    tf.SetFaceName("Consolas");
    title->SetFont(tf);
    hRow->Add(title, 1, wxALIGN_CENTER_VERTICAL|wxLEFT, 12);

    auto* settBtn = new wxButton(header, ID_SETTINGS, "\u2699",
                                  wxDefaultPosition, wxSize(32, 28));
    settBtn->SetBackgroundColour(CLR_PANEL);
    settBtn->SetForegroundColour(CLR_TEXT);
    hRow->Add(settBtn, 0, wxALIGN_CENTER_VERTICAL|wxRIGHT, 6);

    header->SetSizer(hRow);
    root->Add(header, 0, wxEXPAND);

    // ── 타이머 표시 ───────────────────────
    auto* timerPanel = new wxPanel(this, wxID_ANY);
    timerPanel->SetBackgroundColour(CLR_BG);
    auto* tCol = new wxBoxSizer(wxVERTICAL);

    m_timerLabel = new wxStaticText(timerPanel, wxID_ANY, "00:00:00",
                                     wxDefaultPosition, wxDefaultSize, wxALIGN_CENTER);
    wxFont tf2 = m_timerLabel->GetFont();
    tf2.SetPointSize(34); tf2.SetWeight(wxFONTWEIGHT_BOLD);
    tf2.SetFaceName("Consolas");
    m_timerLabel->SetFont(tf2);
    m_timerLabel->SetForegroundColour(CLR_DIM);
    tCol->Add(m_timerLabel, 0, wxALIGN_CENTER|wxTOP, 10);

    m_statusLabel = new wxStaticText(timerPanel, wxID_ANY, "\u25cf \ub300\uae30 \uc911",
                                      wxDefaultPosition, wxDefaultSize, wxALIGN_CENTER);
    m_statusLabel->SetForegroundColour(CLR_DIM);
    tCol->Add(m_statusLabel, 0, wxALIGN_CENTER|wxBOTTOM, 8);

    timerPanel->SetSizer(tCol);
    root->Add(timerPanel, 0, wxEXPAND);

    // ── 오늘 총 시간 ──────────────────────
    auto* todayPanel = new wxPanel(this, wxID_ANY);
    todayPanel->SetBackgroundColour(CLR_PANEL);
    auto* todayRow = new wxBoxSizer(wxHORIZONTAL);

    auto* todayLbl = new wxStaticText(todayPanel, wxID_ANY, "\uc624\ub298 \uc635 \uc791\uc5c5");
    todayLbl->SetForegroundColour(CLR_DIM);
    todayRow->Add(todayLbl, 1, wxALIGN_CENTER_VERTICAL|wxLEFT, 12);

    m_todayLabel = new wxStaticText(todayPanel, wxID_ANY, "00:00:00");
    wxFont tf3 = m_todayLabel->GetFont();
    tf3.SetFaceName("Consolas"); tf3.SetWeight(wxFONTWEIGHT_BOLD);
    m_todayLabel->SetFont(tf3);
    m_todayLabel->SetForegroundColour(CLR_BLUE);
    todayRow->Add(m_todayLabel, 0, wxALIGN_CENTER_VERTICAL|wxRIGHT, 12);

    todayPanel->SetSizer(todayRow);
    root->Add(todayPanel, 0, wxEXPAND);
    UpdateTodayLabel();

    // ── 버튼 ──────────────────────────────
    auto* btnRow = new wxBoxSizer(wxHORIZONTAL);

    m_startBtn = new wxButton(this, ID_TOGGLE, "\u25b6 \uc2dc\uc791");
    m_startBtn->SetBackgroundColour(CLR_RED);
    m_startBtn->SetForegroundColour(*wxWHITE);
    btnRow->Add(m_startBtn, 1, wxEXPAND|wxALL, 6);

    auto* resetBtn = new wxButton(this, ID_RESET, "\u21ba \ub9ac\uc14b");
    resetBtn->SetBackgroundColour(CLR_BLUE);
    resetBtn->SetForegroundColour(CLR_TEXT);
    btnRow->Add(resetBtn, 1, wxEXPAND|wxTOP|wxRIGHT|wxBOTTOM, 6);

    root->Add(btnRow, 0, wxEXPAND|wxLEFT|wxRIGHT, 6);

    // ── 구분선 ────────────────────────────
    root->Add(new wxStaticLine(this), 0, wxEXPAND|wxLEFT|wxRIGHT, 12);

    // ── 작업 앱 목록 헤더 ─────────────────
    auto* listHeader = new wxBoxSizer(wxHORIZONTAL);

    auto* listTitle = new wxStaticText(this, wxID_ANY, "\uc791\uc5c5 \uc571 \ubaa9\ub85d");
    listTitle->SetForegroundColour(CLR_TEXT);
    wxFont lt = listTitle->GetFont();
    lt.SetWeight(wxFONTWEIGHT_BOLD);
    listTitle->SetFont(lt);
    listHeader->Add(listTitle, 1, wxALIGN_CENTER_VERTICAL|wxLEFT, 12);

    auto* addBtn = new wxButton(this, ID_ADD_APP, "+ \uc571 \ucd94\uac00",
                                 wxDefaultPosition, wxSize(-1, 24));
    addBtn->SetBackgroundColour(CLR_BLUE);
    addBtn->SetForegroundColour(wxColour(112, 144, 255));
    listHeader->Add(addBtn, 0, wxALIGN_CENTER_VERTICAL|wxRIGHT, 12);

    root->Add(listHeader, 0, wxEXPAND|wxTOP|wxBOTTOM, 4);

    // ── 앱 리스트박스 ─────────────────────
    m_appList = new wxListBox(this, wxID_ANY, wxDefaultPosition,
                               wxSize(-1, 100), 0, nullptr, wxLB_SINGLE);
    m_appList->SetBackgroundColour(CLR_PANEL);
    m_appList->SetForegroundColour(CLR_TEXT);
    root->Add(m_appList, 1, wxEXPAND|wxLEFT|wxRIGHT|wxBOTTOM, 12);

    m_appList->Bind(wxEVT_LISTBOX_DCLICK, [this](wxCommandEvent& e) {
        OnRemoveApp(e);
    });

    RefreshAppList();

    // ── 하단 힌트 ─────────────────────────
    auto* hint = new wxStaticText(this, wxID_ANY,
        "\ub354\ube14\ud074\ub9ad\uc73c\ub85c \uc571 \uc0ad\uc81c | \ub4f1\ub85d\ub41c \uc571 \ud65c\uc131\ud654 \uc2dc \uc790\ub3d9 \uc2dc\uc791");
    hint->SetForegroundColour(CLR_DIM);
    wxFont hf = hint->GetFont(); hf.SetPointSize(7);
    hint->SetFont(hf);
    root->Add(hint, 0, wxALIGN_CENTER|wxBOTTOM, 4);

    SetSizer(root);
    Layout();
}

// 타이머 세팅.
// 1초마다 OnTick에서 시간 증가, 작업 앱 감지 및 시작/종료 처리
// 작업 앱 감지는 별도 타이머로 1초마다 활성 창 제목과 등록된 키워드 비교
void MainFrame::OnTick(wxTimerEvent&) {
    if (!m_running) return;
    m_elapsed++;
    m_cfg.todayTotal++;
    UpdateDisplay();

    // 색상 알림
    if (m_cfg.colorAlert && m_elapsed > 0 &&
        m_elapsed % (m_cfg.alertMinutes * 60) == 0) {
        m_timerLabel->SetForegroundColour(CLR_ORANGE);
        wxBell();
        // 2초 후 원래 색
        m_ticker.StartOnce(2000);
    }
}

void MainFrame::OnMonitor(wxTimerEvent&) {
    wxString active = GetActiveWindowTitle();
    if (active.IsEmpty()) return;

    wxString matched;
    for (auto& kw : m_cfg.workApps) {
        if (active.Lower().Contains(kw.Lower())) {
            matched = kw;
            break;
        }
    }

    if (!matched.IsEmpty() && !m_running) {
        StartTimer(matched);
    } else if (matched.IsEmpty() && m_running && !m_curApp.IsEmpty()) {
        StopTimer();
    }
}

void MainFrame::StartTimer(const wxString& appName) {
    m_running = true;
    m_curApp  = appName;
    m_startBtn->SetLabel("\u23f8 \uc815\uc9c0");
    m_timerLabel->SetForegroundColour(CLR_RED);
    m_statusLabel->SetForegroundColour(CLR_GREEN);
    wxString s = "\u25cf \uc791\uc5c5 \uc911";
    if (!appName.IsEmpty()) s += ": " + appName.Left(20);
    m_statusLabel->SetLabel(s);
}

void MainFrame::StopTimer() {
    m_running = false;
    if (m_elapsed > 0) {
        Session s;
        s.appName  = m_curApp.IsEmpty() ? "\uc218\ub3d9" : m_curApp;
        s.duration = m_elapsed;
        s.date     = wxDateTime::Now().FormatISODate();
        s.endTime  = wxDateTime::Now().Format("%H:%M");
        m_sessions.push_back(s);
        SaveConfig(m_cfg, m_sessions);
    }
    m_startBtn->SetLabel("\u25b6 \uc2dc\uc791");
    m_timerLabel->SetForegroundColour(wxColour(136, 102, 68));
    m_statusLabel->SetForegroundColour(CLR_ORANGE);
    m_statusLabel->SetLabel("\u25cf \uc77c\uc2dc\uc815\uc9c0");
}

void MainFrame::ResetTimer() {
    bool was = m_running;
    if (was) StopTimer();
    m_elapsed = 0;
    m_timerLabel->SetLabel("00:00:00");
    m_timerLabel->SetForegroundColour(CLR_DIM);
    m_statusLabel->SetLabel("\u25cf \ub300\uae30 \uc911");
    m_statusLabel->SetForegroundColour(CLR_DIM);
    m_startBtn->SetLabel("\u25b6 \uc2dc\uc791");
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

void MainFrame::RefreshAppList() {
    m_appList->Clear();
    for (auto& a : m_cfg.workApps)
        m_appList->Append("  " + a);
}

// ─────────────────────────────────────────
// 이벤트 핸들러
// ─────────────────────────────────────────
void MainFrame::OnToggle(wxCommandEvent&) {
    if (m_running) StopTimer();
    else           StartTimer();
}

void MainFrame::OnReset(wxCommandEvent&) { ResetTimer(); }

void MainFrame::OnAddApp(wxCommandEvent&) {
    AddAppDialog dlg(this);
    if (dlg.ShowModal() == wxID_OK && !dlg.result.IsEmpty()) {
        wxString kw = dlg.result;
        for (auto& a : m_cfg.workApps) {
            if (a == kw) {
                wxMessageBox("이미 등록된 키워드입니다.", "알림");
                return;
            }
        }
        m_cfg.workApps.push_back(kw);
        SaveConfig(m_cfg, m_sessions);
        RefreshAppList();
    }
}

void MainFrame::OnRemoveApp(wxCommandEvent&) {
    int sel = m_appList->GetSelection();
    if (sel == wxNOT_FOUND) return;
    wxString app = m_cfg.workApps[sel];
    if (wxMessageBox(
            "'" + app + "' 을(를) 목록에서 삭제할까요?",
            "삭제 확인", wxYES_NO | wxICON_QUESTION) == wxYES) {
        m_cfg.workApps.erase(m_cfg.workApps.begin() + sel);
        SaveConfig(m_cfg, m_sessions);
        RefreshAppList();
    }
}

void MainFrame::OnSettings(wxCommandEvent&) {
    wxDialog dlg(this, wxID_ANY, "설정", wxDefaultPosition, wxSize(300, 360));
    dlg.SetBackgroundColour(CLR_BG);

    auto* sizer = new wxBoxSizer(wxVERTICAL);

    auto addRow = [&](const wxString& lbl) {
        auto* row = new wxBoxSizer(wxHORIZONTAL);
        auto* l = new wxStaticText(&dlg, wxID_ANY, lbl);
        l->SetForegroundColour(CLR_TEXT);
        row->Add(l, 1, wxALIGN_CENTER_VERTICAL);
        return row;
    };

    sizer->AddSpacer(10);
    auto* titleLbl = new wxStaticText(&dlg, wxID_ANY, "\u2699 설정");
    titleLbl->SetForegroundColour(CLR_RED);
    wxFont tf = titleLbl->GetFont(); tf.SetPointSize(12); tf.SetWeight(wxFONTWEIGHT_BOLD);
    titleLbl->SetFont(tf);
    sizer->Add(titleLbl, 0, wxALIGN_CENTER|wxBOTTOM, 10);

    // 색상 알림 체크박스
    auto* alertCb = new wxCheckBox(&dlg, wxID_ANY, "색상 알림 (작업 지속 경보)");
    alertCb->SetValue(m_cfg.colorAlert);
    alertCb->SetForegroundColour(CLR_TEXT);
    alertCb->SetBackgroundColour(CLR_BG);
    sizer->Add(alertCb, 0, wxLEFT|wxBOTTOM, 16);

    // 알림 간격
    auto* r2 = addRow("알림 간격 (분):");
    auto* spin = new wxSpinCtrl(&dlg, wxID_ANY, wxEmptyString,
                                 wxDefaultPosition, wxSize(60, -1));
    spin->SetRange(1, 120);
    spin->SetValue(m_cfg.alertMinutes);
    spin->SetBackgroundColour(CLR_PANEL);
    spin->SetForegroundColour(*wxWHITE);
    r2->Add(spin, 0);
    sizer->Add(r2, 0, wxLEFT|wxRIGHT|wxBOTTOM, 16);

    // 항상 위
    auto* topmostCb = new wxCheckBox(&dlg, wxID_ANY, "항상 위에 표시");
    topmostCb->SetValue(m_cfg.alwaysOnTop);
    topmostCb->SetForegroundColour(CLR_TEXT);
    topmostCb->SetBackgroundColour(CLR_BG);
    sizer->Add(topmostCb, 0, wxLEFT|wxBOTTOM, 16);

    sizer->Add(new wxStaticLine(&dlg), 0, wxEXPAND|wxLEFT|wxRIGHT, 12);

    // 오늘 통계
    wxString today = wxDateTime::Now().FormatISODate();
    std::map<wxString, int> appTimes;
    for (auto& s : m_sessions) {
        if (s.date == today) appTimes[s.appName] += s.duration;
    }
    wxString stat;
    stat.Printf("오늘 세션: %d회\n", (int)std::count_if(
        m_sessions.begin(), m_sessions.end(),
        [&](const Session& s){ return s.date == today; }));
    for (auto& p : appTimes) {
        stat += wxString::Format("  %s: %s\n",
            p.first.Left(20), FormatTime(p.second));
    }
    auto* statLbl = new wxStaticText(&dlg, wxID_ANY, stat);
    statLbl->SetForegroundColour(CLR_DIM);
    sizer->Add(statLbl, 0, wxLEFT|wxTOP, 12);

    sizer->AddStretchSpacer();

    auto* applyBtn = new wxButton(&dlg, wxID_OK, "적용");
    applyBtn->SetBackgroundColour(CLR_RED);
    applyBtn->SetForegroundColour(*wxWHITE);
    sizer->Add(applyBtn, 0, wxALIGN_CENTER|wxBOTTOM, 12);

    dlg.SetSizer(sizer);

    if (dlg.ShowModal() == wxID_OK) {
        m_cfg.colorAlert   = alertCb->GetValue();
        m_cfg.alertMinutes = spin->GetValue();
        m_cfg.alwaysOnTop  = topmostCb->GetValue();
        long style = GetWindowStyle();
        if (m_cfg.alwaysOnTop) style |=  wxSTAY_ON_TOP;
        else                    style &= ~wxSTAY_ON_TOP;
        SetWindowStyle(style);
        SaveConfig(m_cfg, m_sessions);
    }
}

void MainFrame::OnClose(wxCloseEvent&) {
    if (m_running) StopTimer();
    SaveConfig(m_cfg, m_sessions);
    Destroy();
}

// ─────────────────────────────────────────
// 앱 클래스
// ─────────────────────────────────────────
class WorkTimerApp : public wxApp {
public:
    bool OnInit() override {
        SetAppName("WorkTimer");
        auto* frame = new MainFrame();
        frame->Show(true);
        return true;
    }
};

wxIMPLEMENT_APP(WorkTimerApp);
