// TortoiseGit - a Windows shell extension for easy version control

// Copyright (C) 2003-2013 - TortoiseSVN
// Copyright (C) 2008-2014 - TortoiseGit

// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software Foundation,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
//
#include "stdafx.h"
#include "TortoiseProc.h"
#include "CommitDlg.h"
#include "DirFileEnum.h"
#include "MessageBox.h"
#include "AppUtils.h"
#include "PathUtils.h"
#include "Git.h"
#include "registry.h"
#include "GitStatus.h"
#include "HistoryDlg.h"
#include "Hooks.h"
#include "UnicodeUtils.h"
#include "../TGitCache/CacheInterface.h"
#include "ProgressDlg.h"
#include "ShellUpdater.h"
#include "Commands/PushCommand.h"
#include "PatchViewDlg.h"
#include "COMError.h"
#include "Globals.h"
#include "SysProgressDlg.h"
#include "MassiveGitTask.h"
#include "LogDlg.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

UINT CCommitDlg::WM_AUTOLISTREADY = RegisterWindowMessage(_T("TORTOISEGIT_AUTOLISTREADY_MSG"));
UINT CCommitDlg::WM_UPDATEOKBUTTON = RegisterWindowMessage(_T("TORTOISEGIT_COMMIT_UPDATEOKBUTTON"));
UINT CCommitDlg::WM_UPDATEDATAFALSE = RegisterWindowMessage(_T("TORTOISEGIT_COMMIT_UPDATEDATAFALSE"));

IMPLEMENT_DYNAMIC(CCommitDlg, CResizableStandAloneDialog)
CCommitDlg::CCommitDlg(CWnd* pParent /*=NULL*/)
	: CResizableStandAloneDialog(CCommitDlg::IDD, pParent)
	, m_bRecursive(FALSE)
	, m_bShowUnversioned(FALSE)
	, m_bBlock(FALSE)
	, m_bThreadRunning(FALSE)
	, m_bRunThread(FALSE)
	, m_pThread(NULL)
	, m_bWholeProject(FALSE)
	, m_bKeepChangeList(TRUE)
	, m_bDoNotAutoselectSubmodules(FALSE)
	, m_itemsCount(0)
	, m_bSelectFilesForCommit(TRUE)
	, m_bNoPostActions(FALSE)
	, m_bAutoClose(false)
	, m_bSetCommitDateTime(FALSE)
	, m_bCreateNewBranch(FALSE)
	, m_bForceCommitAmend(false)
	, m_bCommitMessageOnly(FALSE)
	, m_bSetAuthor(FALSE)
	, m_bCancelled(false)
	, m_PostCmd(GIT_POSTCOMMIT_CMD_NOTHING)
	, m_bAmendDiffToLastCommit(FALSE)
	, m_nPopupPasteListCmd(0)
	, m_nPopupPasteLastMessage(0)
	, m_nPopupRecentMessage(0)
	, m_nPopupPickCommitHash(0)
	, m_nPopupPickCommitMessage(0)
	, m_hAccel(nullptr)
{
	this->m_bCommitAmend=FALSE;
}

CCommitDlg::~CCommitDlg()
{
	if(m_pThread != NULL)
	{
		delete m_pThread;
	}
}

void CCommitDlg::DoDataExchange(CDataExchange* pDX)
{
	CResizableStandAloneDialog::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_FILELIST, m_ListCtrl);
	DDX_Control(pDX, IDC_LOGMESSAGE, m_cLogMessage);
	DDX_Check(pDX, IDC_SHOWUNVERSIONED, m_bShowUnversioned);
	DDX_Check(pDX, IDC_COMMIT_SETDATETIME, m_bSetCommitDateTime);
	DDX_Check(pDX, IDC_CHECK_NEWBRANCH, m_bCreateNewBranch);
	DDX_Text(pDX, IDC_NEWBRANCH, m_sCreateNewBranch);
	DDX_Text(pDX, IDC_BUGID, m_sBugID);
	DDX_Text(pDX, IDC_COMMIT_AUTHORDATA, m_sAuthor);
	DDX_Check(pDX, IDC_WHOLE_PROJECT, m_bWholeProject);
	DDX_Control(pDX, IDC_SPLITTER, m_wndSplitter);
	DDX_Check(pDX, IDC_KEEPLISTS, m_bKeepChangeList);
	DDX_Check(pDX, IDC_NOAUTOSELECTSUBMODULES, m_bDoNotAutoselectSubmodules);
	DDX_Check(pDX,IDC_COMMIT_AMEND,m_bCommitAmend);
	DDX_Check(pDX, IDC_COMMIT_MESSAGEONLY, m_bCommitMessageOnly);
	DDX_Check(pDX,IDC_COMMIT_AMENDDIFF,m_bAmendDiffToLastCommit);
	DDX_Check(pDX, IDC_COMMIT_SETAUTHOR, m_bSetAuthor);
	DDX_Control(pDX,IDC_VIEW_PATCH,m_ctrlShowPatch);
	DDX_Control(pDX, IDC_COMMIT_DATEPICKER, m_CommitDate);
	DDX_Control(pDX, IDC_COMMIT_TIMEPICKER, m_CommitTime);
}

BEGIN_MESSAGE_MAP(CCommitDlg, CResizableStandAloneDialog)
	ON_BN_CLICKED(IDC_SHOWUNVERSIONED, OnBnClickedShowunversioned)
	ON_NOTIFY(SCN_UPDATEUI, IDC_LOGMESSAGE, OnScnUpdateUI)
//	ON_BN_CLICKED(IDC_HISTORY, OnBnClickedHistory)
	ON_BN_CLICKED(IDC_BUGTRAQBUTTON, OnBnClickedBugtraqbutton)
	ON_EN_CHANGE(IDC_LOGMESSAGE, OnEnChangeLogmessage)
	ON_REGISTERED_MESSAGE(CGitStatusListCtrl::GITSLNM_ITEMCOUNTCHANGED, OnGitStatusListCtrlItemCountChanged)
	ON_REGISTERED_MESSAGE(CGitStatusListCtrl::GITSLNM_NEEDSREFRESH, OnGitStatusListCtrlNeedsRefresh)
	ON_REGISTERED_MESSAGE(CGitStatusListCtrl::GITSLNM_ADDFILE, OnFileDropped)
	ON_REGISTERED_MESSAGE(CGitStatusListCtrl::GITSLNM_CHECKCHANGED, &CCommitDlg::OnGitStatusListCtrlCheckChanged)
	ON_REGISTERED_MESSAGE(CGitStatusListCtrl::GITSLNM_ITEMCHANGED, &CCommitDlg::OnGitStatusListCtrlItemChanged)

	ON_REGISTERED_MESSAGE(CLinkControl::LK_LINKITEMCLICKED, &CCommitDlg::OnCheck)
	ON_REGISTERED_MESSAGE(WM_AUTOLISTREADY, OnAutoListReady)
	ON_REGISTERED_MESSAGE(WM_UPDATEOKBUTTON, OnUpdateOKButton)
	ON_REGISTERED_MESSAGE(WM_UPDATEDATAFALSE, OnUpdateDataFalse)
	ON_WM_TIMER()
	ON_WM_SIZE()
	ON_STN_CLICKED(IDC_EXTERNALWARNING, &CCommitDlg::OnStnClickedExternalwarning)
	ON_BN_CLICKED(IDC_SIGNOFF, &CCommitDlg::OnBnClickedSignOff)
	ON_BN_CLICKED(IDC_COMMIT_AMEND, &CCommitDlg::OnBnClickedCommitAmend)
	ON_BN_CLICKED(IDC_COMMIT_MESSAGEONLY, &CCommitDlg::OnBnClickedCommitMessageOnly)
	ON_BN_CLICKED(IDC_WHOLE_PROJECT, &CCommitDlg::OnBnClickedWholeProject)
	ON_COMMAND(ID_FOCUS_MESSAGE,&CCommitDlg::OnFocusMessage)
	ON_COMMAND(ID_FOCUS_FILELIST, OnFocusFileList)
	ON_STN_CLICKED(IDC_VIEW_PATCH, &CCommitDlg::OnStnClickedViewPatch)
	ON_WM_MOVE()
	ON_WM_MOVING()
	ON_WM_SIZING()
	ON_NOTIFY(HDN_ITEMCHANGED, 0, &CCommitDlg::OnHdnItemchangedFilelist)
	ON_BN_CLICKED(IDC_COMMIT_AMENDDIFF, &CCommitDlg::OnBnClickedCommitAmenddiff)
	ON_BN_CLICKED(IDC_NOAUTOSELECTSUBMODULES, &CCommitDlg::OnBnClickedNoautoselectsubmodules)
	ON_BN_CLICKED(IDC_COMMIT_SETDATETIME, &CCommitDlg::OnBnClickedCommitSetDateTime)
	ON_BN_CLICKED(IDC_CHECK_NEWBRANCH, &CCommitDlg::OnBnClickedCheckNewBranch)
	ON_BN_CLICKED(IDC_COMMIT_SETAUTHOR, &CCommitDlg::OnBnClickedCommitSetauthor)
END_MESSAGE_MAP()

bool PrefillMessage(const CString &filename, CString &msg)
{
	if (PathFileExists(filename))
	{
		CStdioFile file;
		if (file.Open(filename, CFile::modeRead | CFile::typeBinary))
		{
			CString str;
			std::unique_ptr<BYTE[]> buf(new BYTE[file.GetLength()]);
			UINT read = file.Read(buf.get(), (UINT)file.GetLength());
			g_Git.StringAppend(&str, buf.get(), CP_UTF8, read);
			str.Replace(_T("\r\n"), _T("\n"));
			str.TrimRight(_T("\n"));
			str += _T("\n");
			msg = str;
		}
		else
			::MessageBox(nullptr, _T("Could not open ") + filename, _T("TortoiseGit"), MB_ICONERROR);
		return true; // load no further files
	}
	return false;
}

int GetCommitTemplate(CString &msg)
{
	CString tplFilename = g_Git.GetConfigValue(_T("commit.template"));
	if (tplFilename.IsEmpty())
		return -1;

	if (tplFilename[0] == _T('/'))
	{
		if (tplFilename.GetLength() >= 3)
		{
			// handle "/d/TortoiseGit/tpl.txt" -> "d:/TortoiseGit/tpl.txt"
			if (tplFilename[2] == _T('/'))
			{
				tplFilename.GetBuffer()[0] = tplFilename[1];
				tplFilename.GetBuffer()[1] = _T(':');
			}
		}
	}

	tplFilename.Replace(_T('/'), _T('\\'));

	if (!PrefillMessage(tplFilename, msg))
		return -1;
	return 0;
}

BOOL CCommitDlg::OnInitDialog()
{
	CResizableStandAloneDialog::OnInitDialog();
	CAppUtils::MarkWindowAsUnpinnable(m_hWnd);

	if (!m_bForceCommitAmend)
	{
		GetCommitTemplate(this->m_sLogMessage);
		m_sLogMessage = m_sLogMessage;
	}

	CString dotGitPath;
	g_GitAdminDir.GetAdminDirPath(g_Git.m_CurrentDir, dotGitPath);
	bool loadedMsg = !PrefillMessage(dotGitPath + _T("MERGE_MSG"), m_sLogMessage);
	loadedMsg = loadedMsg && !PrefillMessage(dotGitPath + _T("SQUASH_MSG"), m_sLogMessage);

	if (CTGitPath(g_Git.m_CurrentDir).IsMergeActive())
	{
		DialogEnableWindow(IDC_CHECK_NEWBRANCH, FALSE);
		m_bCreateNewBranch = FALSE;
		GetDlgItem(IDC_MERGEACTIVE)->ShowWindow(SW_SHOW);
	}

	m_regAddBeforeCommit = CRegDWORD(_T("Software\\TortoiseGit\\AddBeforeCommit"), TRUE);
	m_bShowUnversioned = m_regAddBeforeCommit;

	CString regPath(g_Git.m_CurrentDir);
	regPath.Replace(_T(":"), _T("_"));
	m_regShowWholeProject = CRegDWORD(_T("Software\\TortoiseGit\\TortoiseProc\\ShowWholeProject\\") + regPath, FALSE);
	m_bWholeProject = m_regShowWholeProject;

	m_History.SetMaxHistoryItems((LONG)CRegDWORD(_T("Software\\TortoiseGit\\MaxHistoryItems"), 25));

	m_regKeepChangelists = CRegDWORD(_T("Software\\TortoiseGit\\KeepChangeLists"), FALSE);
	m_bKeepChangeList = m_regKeepChangelists;

	m_regDoNotAutoselectSubmodules = CRegDWORD(_T("Software\\TortoiseGit\\DoNotAutoselectSubmodules"), FALSE);
	m_bDoNotAutoselectSubmodules = m_regDoNotAutoselectSubmodules;

	m_hAccel = LoadAccelerators(AfxGetResourceHandle(),MAKEINTRESOURCE(IDR_ACC_COMMITDLG));

//	GitConfig config;
//	m_bWholeProject = config.KeepLocks();

	SetDlgTitle();

	UpdateData(FALSE);

	m_ListCtrl.Init(GITSLC_COLEXT | GITSLC_COLSTATUS | GITSLC_COLADD | GITSLC_COLDEL, _T("CommitDlg"),(GITSLC_POPALL ^ (GITSLC_POPCOMMIT | GITSLC_POPSAVEAS)), true, true);
	m_ListCtrl.SetStatLabel(GetDlgItem(IDC_STATISTICS));
	m_ListCtrl.SetCancelBool(&m_bCancelled);
	m_ListCtrl.SetEmptyString(IDS_COMMITDLG_NOTHINGTOCOMMIT);
	m_ListCtrl.EnableFileDrop();
	m_ListCtrl.SetBackgroundImage(IDI_COMMIT_BKG);

	//this->DialogEnableWindow(IDC_COMMIT_AMEND,FALSE);
	m_ProjectProperties.ReadProps();

	m_cLogMessage.Init(m_ProjectProperties);
	m_cLogMessage.SetFont((CString)CRegString(_T("Software\\TortoiseGit\\LogFontName"), _T("Courier New")), (DWORD)CRegDWORD(_T("Software\\TortoiseGit\\LogFontSize"), 8));
	m_cLogMessage.RegisterContextMenuHandler(this);

	OnEnChangeLogmessage();

	m_tooltips.Create(this);
	m_tooltips.AddTool(IDC_EXTERNALWARNING, IDS_COMMITDLG_EXTERNALS);
	m_tooltips.AddTool(IDC_COMMIT_AMEND,IDS_COMMIT_AMEND_TT);
	m_tooltips.AddTool(IDC_MERGEACTIVE, IDC_MERGEACTIVE_TT);
//	m_tooltips.AddTool(IDC_HISTORY, IDS_COMMITDLG_HISTORY_TT);

	CBugTraqAssociations bugtraq_associations;
	bugtraq_associations.Load(m_ProjectProperties.GetProviderUUID(), m_ProjectProperties.sProviderParams);

	if (bugtraq_associations.FindProvider(g_Git.m_CurrentDir, &m_bugtraq_association))
	{
		GetDlgItem(IDC_BUGID)->ShowWindow(SW_HIDE);
		GetDlgItem(IDC_BUGIDLABEL)->ShowWindow(SW_HIDE);

		CComPtr<IBugTraqProvider> pProvider;
		HRESULT hr = pProvider.CoCreateInstance(m_bugtraq_association.GetProviderClass());
		if (SUCCEEDED(hr))
		{
			m_BugTraqProvider = pProvider;
			BSTR temp = NULL;
			if (SUCCEEDED(hr = pProvider->GetLinkText(GetSafeHwnd(), m_bugtraq_association.GetParameters().AllocSysString(), &temp)))
			{
				SetDlgItemText(IDC_BUGTRAQBUTTON, temp);
				GetDlgItem(IDC_BUGTRAQBUTTON)->EnableWindow(TRUE);
				GetDlgItem(IDC_BUGTRAQBUTTON)->ShowWindow(SW_SHOW);
			}

			SysFreeString(temp);
		}

		GetDlgItem(IDC_LOGMESSAGE)->SetFocus();
	}
	else if (!m_ProjectProperties.sMessage.IsEmpty())
	{
		GetDlgItem(IDC_BUGID)->ShowWindow(SW_SHOW);
		GetDlgItem(IDC_BUGIDLABEL)->ShowWindow(SW_SHOW);
		if (!m_ProjectProperties.sLabel.IsEmpty())
			SetDlgItemText(IDC_BUGIDLABEL, m_ProjectProperties.sLabel);
		GetDlgItem(IDC_BUGTRAQBUTTON)->ShowWindow(SW_HIDE);
		GetDlgItem(IDC_BUGTRAQBUTTON)->EnableWindow(FALSE);
		GetDlgItem(IDC_BUGID)->SetFocus();
		CString sBugID = m_ProjectProperties.GetBugIDFromLog(m_sLogMessage);
		if (!sBugID.IsEmpty())
		{
			SetDlgItemText(IDC_BUGID, sBugID);
		}
	}
	else
	{
		GetDlgItem(IDC_BUGID)->ShowWindow(SW_HIDE);
		GetDlgItem(IDC_BUGIDLABEL)->ShowWindow(SW_HIDE);
		GetDlgItem(IDC_BUGTRAQBUTTON)->ShowWindow(SW_HIDE);
		GetDlgItem(IDC_BUGTRAQBUTTON)->EnableWindow(FALSE);
		GetDlgItem(IDC_LOGMESSAGE)->SetFocus();
	}

	if (!m_sLogMessage.IsEmpty())
	{
		m_cLogMessage.SetText(m_sLogMessage);
		m_cLogMessage.Call(SCI_SETCURRENTPOS, 0);
		m_cLogMessage.Call(SCI_SETSEL, 0, 0);
	}

	GetWindowText(m_sWindowTitle);

	AdjustControlSize(IDC_SHOWUNVERSIONED);
	AdjustControlSize(IDC_WHOLE_PROJECT);
	AdjustControlSize(IDC_CHECK_NEWBRANCH);
	AdjustControlSize(IDC_COMMIT_AMEND);
	AdjustControlSize(IDC_COMMIT_MESSAGEONLY);
	AdjustControlSize(IDC_COMMIT_AMENDDIFF);
	AdjustControlSize(IDC_COMMIT_SETDATETIME);
	AdjustControlSize(IDC_COMMIT_SETAUTHOR);
	AdjustControlSize(IDC_NOAUTOSELECTSUBMODULES);
	AdjustControlSize(IDC_KEEPLISTS);

	m_linkControl.ConvertStaticToLink(m_hWnd, IDC_CHECKALL);
	m_linkControl.ConvertStaticToLink(m_hWnd, IDC_CHECKNONE);
	m_linkControl.ConvertStaticToLink(m_hWnd, IDC_CHECKUNVERSIONED);
	m_linkControl.ConvertStaticToLink(m_hWnd, IDC_CHECKVERSIONED);
	m_linkControl.ConvertStaticToLink(m_hWnd, IDC_CHECKADDED);
	m_linkControl.ConvertStaticToLink(m_hWnd, IDC_CHECKDELETED);
	m_linkControl.ConvertStaticToLink(m_hWnd, IDC_CHECKMODIFIED);
	m_linkControl.ConvertStaticToLink(m_hWnd, IDC_CHECKFILES);
	m_linkControl.ConvertStaticToLink(m_hWnd, IDC_CHECKSUBMODULES);

	// line up all controls and adjust their sizes.
#define LINKSPACING 9
	RECT rc = AdjustControlSize(IDC_SELECTLABEL);
	rc.right -= 15;	// AdjustControlSize() adds 20 pixels for the checkbox/radio button bitmap, but this is a label...
	rc = AdjustStaticSize(IDC_CHECKALL, rc, LINKSPACING);
	rc = AdjustStaticSize(IDC_CHECKNONE, rc, LINKSPACING);
	rc = AdjustStaticSize(IDC_CHECKUNVERSIONED, rc, LINKSPACING);
	rc = AdjustStaticSize(IDC_CHECKVERSIONED, rc, LINKSPACING);
	rc = AdjustStaticSize(IDC_CHECKADDED, rc, LINKSPACING);
	rc = AdjustStaticSize(IDC_CHECKDELETED, rc, LINKSPACING);
	rc = AdjustStaticSize(IDC_CHECKMODIFIED, rc, LINKSPACING);
	rc = AdjustStaticSize(IDC_CHECKFILES, rc, LINKSPACING);
	rc = AdjustStaticSize(IDC_CHECKSUBMODULES, rc, LINKSPACING);

	GetClientRect(m_DlgOrigRect);
	m_cLogMessage.GetClientRect(m_LogMsgOrigRect);

	AddAnchor(IDC_COMMITLABEL, TOP_LEFT, TOP_RIGHT);
	AddAnchor(IDC_BUGIDLABEL, TOP_RIGHT);
	AddAnchor(IDC_BUGID, TOP_RIGHT);
	AddAnchor(IDC_BUGTRAQBUTTON, TOP_RIGHT);
	AddAnchor(IDC_COMMIT_TO, TOP_LEFT, TOP_RIGHT);
	AddAnchor(IDC_CHECK_NEWBRANCH, TOP_RIGHT);
	AddAnchor(IDC_NEWBRANCH, TOP_LEFT, TOP_RIGHT);
	AddAnchor(IDC_MESSAGEGROUP, TOP_LEFT, TOP_RIGHT);
//	AddAnchor(IDC_HISTORY, TOP_LEFT);
	AddAnchor(IDC_LOGMESSAGE, TOP_LEFT, TOP_RIGHT);
	AddAnchor(IDC_SIGNOFF, TOP_RIGHT);
	AddAnchor(IDC_VIEW_PATCH, BOTTOM_RIGHT);
	AddAnchor(IDC_LISTGROUP, TOP_LEFT, BOTTOM_RIGHT);
	AddAnchor(IDC_SPLITTER, TOP_LEFT, TOP_RIGHT);
	AddAnchor(IDC_FILELIST, TOP_LEFT, BOTTOM_RIGHT);
	AddAnchor(IDC_SHOWUNVERSIONED, BOTTOM_LEFT);
	AddAnchor(IDC_EXTERNALWARNING, BOTTOM_RIGHT);
	AddAnchor(IDC_STATISTICS, BOTTOM_LEFT, BOTTOM_RIGHT);
	AddAnchor(IDC_TEXT_INFO, TOP_RIGHT);
	AddAnchor(IDC_WHOLE_PROJECT, BOTTOM_LEFT);
	AddAnchor(IDC_KEEPLISTS, BOTTOM_LEFT);
	AddAnchor(IDC_NOAUTOSELECTSUBMODULES, BOTTOM_LEFT);
	AddAnchor(IDOK, BOTTOM_RIGHT);
	AddAnchor(IDCANCEL, BOTTOM_RIGHT);
	AddAnchor(IDHELP, BOTTOM_RIGHT);
	AddAnchor(IDC_MERGEACTIVE, BOTTOM_RIGHT);
	AddAnchor(IDC_COMMIT_AMEND,TOP_LEFT);
	AddAnchor(IDC_COMMIT_MESSAGEONLY, BOTTOM_LEFT);
	AddAnchor(IDC_COMMIT_AMENDDIFF,TOP_LEFT);
	AddAnchor(IDC_COMMIT_SETDATETIME,TOP_LEFT);
	AddAnchor(IDC_COMMIT_DATEPICKER,TOP_LEFT);
	AddAnchor(IDC_COMMIT_TIMEPICKER,TOP_LEFT);
	AddAnchor(IDC_COMMIT_SETAUTHOR, TOP_LEFT);
	AddAnchor(IDC_COMMIT_AUTHORDATA, TOP_LEFT, TOP_RIGHT);

	AddAnchor(IDC_SELECTLABEL, TOP_LEFT);
	AddAnchor(IDC_CHECKALL, TOP_LEFT);
	AddAnchor(IDC_CHECKNONE, TOP_LEFT);
	AddAnchor(IDC_CHECKUNVERSIONED, TOP_LEFT);
	AddAnchor(IDC_CHECKVERSIONED, TOP_LEFT);
	AddAnchor(IDC_CHECKADDED, TOP_LEFT);
	AddAnchor(IDC_CHECKDELETED, TOP_LEFT);
	AddAnchor(IDC_CHECKMODIFIED, TOP_LEFT);
	AddAnchor(IDC_CHECKFILES, TOP_LEFT);
	AddAnchor(IDC_CHECKSUBMODULES, TOP_LEFT);

	if (hWndExplorer)
		CenterWindow(CWnd::FromHandle(hWndExplorer));
	EnableSaveRestore(_T("CommitDlg"));
	DWORD yPos = CRegDWORD(_T("Software\\TortoiseGit\\TortoiseProc\\ResizableState\\CommitDlgSizer"));
	RECT rcDlg, rcLogMsg, rcFileList;
	GetClientRect(&rcDlg);
	m_cLogMessage.GetWindowRect(&rcLogMsg);
	ScreenToClient(&rcLogMsg);
	m_ListCtrl.GetWindowRect(&rcFileList);
	ScreenToClient(&rcFileList);
	if (yPos)
	{
		RECT rectSplitter;
		m_wndSplitter.GetWindowRect(&rectSplitter);
		ScreenToClient(&rectSplitter);
		int delta = yPos - rectSplitter.top;
		if ((rcLogMsg.bottom + delta > rcLogMsg.top)&&(rcLogMsg.bottom + delta < rcFileList.bottom - 30))
		{
			m_wndSplitter.SetWindowPos(NULL, rectSplitter.left, yPos, 0, 0, SWP_NOSIZE);
			DoSize(delta);
		}
	}

	SetSplitterRange();

	// add all directories to the watcher
	/*
	for (int i=0; i<m_pathList.GetCount(); ++i)
	{
		if (m_pathList[i].IsDirectory())
			m_pathwatcher.AddPath(m_pathList[i]);
	}*/

	m_updatedPathList = m_pathList;

	//first start a thread to obtain the file list with the status without
	//blocking the dialog
	InterlockedExchange(&m_bBlock, TRUE);
	m_pThread = AfxBeginThread(StatusThreadEntry, this, THREAD_PRIORITY_NORMAL,0,CREATE_SUSPENDED);
	if (m_pThread==NULL)
	{
		CMessageBox::Show(this->m_hWnd, IDS_ERR_THREADSTARTFAILED, IDS_APPNAME, MB_OK | MB_ICONERROR);
		InterlockedExchange(&m_bBlock, FALSE);
	}
	else
	{
		m_pThread->m_bAutoDelete = FALSE;
		m_pThread->ResumeThread();
	}
	CRegDWORD err = CRegDWORD(_T("Software\\TortoiseGit\\ErrorOccurred"), FALSE);
	CRegDWORD historyhint = CRegDWORD(_T("Software\\TortoiseGit\\HistoryHintShown"), FALSE);
	if ((((DWORD)err)!=FALSE)&&((((DWORD)historyhint)==FALSE)))
	{
		historyhint = TRUE;
//		ShowBalloon(IDC_HISTORY, IDS_COMMITDLG_HISTORYHINT_TT, IDI_INFORMATION);
	}
	err = FALSE;

	this->m_ctrlShowPatch.SetURL(CString());

	if (g_Git.GetConfigValueBool(_T("tgit.commitshowpatch")))
		OnStnClickedViewPatch();

	return FALSE;  // return TRUE unless you set the focus to a control
	// EXCEPTION: OCX Property Pages should return FALSE
}

static bool UpdateIndex(CMassiveGitTask &mgt, CSysProgressDlg &sysProgressDlg, int progress, int maxProgress)
{
	if (sysProgressDlg.HasUserCancelled())
		return false;

	if (sysProgressDlg.IsVisible())
	{
		sysProgressDlg.SetTitle(IDS_APPNAME);
		sysProgressDlg.SetLine(1, CString(MAKEINTRESOURCE(IDS_PROC_COMMIT_PREPARECOMMIT)));
		sysProgressDlg.SetLine(2, CString(MAKEINTRESOURCE(IDS_PROC_COMMIT_UPDATEINDEX)));
		sysProgressDlg.SetProgress(progress, maxProgress);
		AfxGetThread()->PumpMessage(); // process messages, in order to avoid freezing
	}

	BOOL cancel = FALSE;
	return mgt.Execute(cancel);
}

void CCommitDlg::OnOK()
{
	if (m_bBlock)
		return;
	if (m_bThreadRunning)
	{
		m_bCancelled = true;
		InterlockedExchange(&m_bRunThread, FALSE);
		WaitForSingleObject(m_pThread->m_hThread, 1000);
		if (m_bThreadRunning)
		{
			// we gave the thread a chance to quit. Since the thread didn't
			// listen to us we have to kill it.
			TerminateThread(m_pThread->m_hThread, (DWORD)-1);
			InterlockedExchange(&m_bThreadRunning, FALSE);
		}
	}
	this->UpdateData();

	if (m_bCreateNewBranch)
	{
		if (!g_Git.IsBranchNameValid(m_sCreateNewBranch))
		{
			ShowEditBalloon(IDC_NEWBRANCH, IDS_B_T_NOTEMPTY, IDS_ERR_ERROR, TTI_ERROR);
			return;
		}
		if (g_Git.BranchTagExists(m_sCreateNewBranch))
		{
			// branch already exists
			CString msg;
			msg.LoadString(IDS_B_EXISTS);
			msg += _T(" ") + CString(MAKEINTRESOURCE(IDS_B_DELETEORDIFFERENTNAME));
			ShowEditBalloon(IDC_NEWBRANCH, msg, CString(MAKEINTRESOURCE(IDS_WARN_WARNING)));
			return;
		}
		if (g_Git.BranchTagExists(m_sCreateNewBranch, false))
		{
			// tag with the same name exists -> shortref is ambiguous
			if (CMessageBox::Show(m_hWnd, IDS_B_SAMETAGNAMEEXISTS, IDS_APPNAME, 2, IDI_EXCLAMATION, IDS_CONTINUEBUTTON, IDS_ABORTBUTTON) == 2)
				return;
		}
	}

	CString id;
	GetDlgItemText(IDC_BUGID, id);
	if (!m_ProjectProperties.CheckBugID(id))
	{
		ShowEditBalloon(IDC_BUGID, IDS_COMMITDLG_ONLYNUMBERS, IDS_ERR_ERROR, TTI_ERROR);
		return;
	}
	m_sLogMessage = m_cLogMessage.GetText();
	if ( m_sLogMessage.IsEmpty() )
	{
		// no message entered, go round again
		CMessageBox::Show(this->m_hWnd, IDS_COMMITDLG_NOMESSAGE, IDS_APPNAME, MB_OK | MB_ICONERROR);
		return;
	}
	if ((m_ProjectProperties.bWarnIfNoIssue) && (id.IsEmpty() && !m_ProjectProperties.HasBugID(m_sLogMessage)))
	{
		if (CMessageBox::Show(this->m_hWnd, IDS_COMMITDLG_NOISSUEWARNING, IDS_APPNAME, MB_YESNO | MB_ICONWARNING)!=IDYES)
			return;
	}

	if (m_ProjectProperties.bWarnNoSignedOffBy == TRUE && m_cLogMessage.GetText().Find(GetSignedOffByLine()) == -1)
	{
		UINT retval = CMessageBox::Show(this->m_hWnd, IDS_PROC_COMMIT_NOSIGNOFFLINE, IDS_APPNAME, 1, IDI_WARNING, IDS_PROC_COMMIT_ADDSIGNOFFBUTTON, IDS_PROC_COMMIT_NOADDSIGNOFFBUTTON, IDS_ABORTBUTTON);
		if (retval == 1)
		{
			OnBnClickedSignOff();
			m_sLogMessage = m_cLogMessage.GetText();
		}
		else if (retval == 3)
			return;
	}

	int nListItems = m_ListCtrl.GetItemCount();
	bool needResetIndex = false;
	for (int i = 0; i < nListItems && !m_bCommitMessageOnly; ++i)
	{
		CTGitPath *entry = (CTGitPath *)m_ListCtrl.GetItemData(i);
		if (!entry->m_Checked && !(entry->m_Action & CTGitPath::LOGACTIONS_UNVER))
			needResetIndex = true;
		if (!entry->m_Checked || !entry->IsDirectory())
			continue;

		bool dirty = false;
		if (entry->m_Action & CTGitPath::LOGACTIONS_UNVER)
		{
			CGit subgit;
			subgit.m_CurrentDir = g_Git.m_CurrentDir + _T("\\") + entry->GetWinPathString();
			CString subcmdout;
			subgit.Run(_T("git.exe status --porcelain"), &subcmdout, CP_UTF8);
			dirty = !subcmdout.IsEmpty();
		}
		else
		{
			CString cmd, cmdout;
			cmd.Format(_T("git.exe diff -- \"%s\""), entry->GetWinPathString());
			g_Git.Run(cmd, &cmdout, CP_UTF8);
			dirty = cmdout.Right(7) == _T("-dirty\n");
		}

		if (dirty)
		{
			CString message;
			message.Format(CString(MAKEINTRESOURCE(IDS_COMMITDLG_SUBMODULEDIRTY)), entry->GetGitPathString());
			int result = CMessageBox::Show(m_hWnd, message, _T("TortoiseGit"), 1, IDI_QUESTION, CString(MAKEINTRESOURCE(IDS_PROGRS_CMD_COMMIT)), CString(MAKEINTRESOURCE(IDS_MSGBOX_IGNORE)), CString(MAKEINTRESOURCE(IDS_MSGBOX_CANCEL)));
			if (result == 1)
			{
				CString cmdCommit;
				cmdCommit.Format(_T("/command:commit /path:\"%s\\%s\""), g_Git.m_CurrentDir, entry->GetWinPathString());
				CAppUtils::RunTortoiseGitProc(cmdCommit);
				return;
			}
			else if (result == 2)
				continue;
			else
				return;
		}
	}

	if (!m_bCommitMessageOnly)
		m_ListCtrl.WriteCheckedNamesToPathList(m_selectedPathList);
	m_pathwatcher.Stop();
	InterlockedExchange(&m_bBlock, TRUE);
	CDWordArray arDeleted;
	//first add all the unversioned files the user selected
	//and check if all versioned files are selected
	int nchecked = 0;
	m_bRecursive = true;

	CTGitPathList itemsToAdd;
	CTGitPathList itemsToRemove;
	CMassiveGitTask mgtReAddAfterCommit(_T("add --ignore-errors -f"));

	CString cmd;
	CString out;

	bool bAddSuccess=true;
	bool bCloseCommitDlg=false;

	CSysProgressDlg sysProgressDlg;
	if (nListItems >= 25)
	{
		sysProgressDlg.SetTitle(CString(MAKEINTRESOURCE(IDS_PROC_COMMIT_PREPARECOMMIT)));
		sysProgressDlg.SetLine(1, CString(MAKEINTRESOURCE(IDS_PROC_COMMIT_UPDATEINDEX)));
		sysProgressDlg.SetTime(true);
		sysProgressDlg.SetShowProgressBar(true);
		sysProgressDlg.ShowModal(this, true);
	}

	CBlockCacheForPath cacheBlock(g_Git.m_CurrentDir);
	DWORD currentTicks = GetTickCount();

	if (g_Git.UsingLibGit2(CGit::GIT_CMD_COMMIT_UPDATE_INDEX))
	{
		/*
			Do not use the libgit2 implementation right now, since it has several flaws:
			* https://github.com/libgit2/libgit2/issues/1397: crlf issue
			* http://code.google.com/p/tortoisegit/issues/detail?id=1690: possible access denied problem
			* https://github.com/libgit2/libgit2/pull/1291: git.exe path is searched again and again
			* changes to x-bit are not correctly committed
		*/
		bAddSuccess = false;
		do
		{
			git_repository *repository = nullptr;
			CStringA gitdir = CUnicodeUtils::GetMulti(CTGitPath(g_Git.m_CurrentDir).GetGitPathString(), CP_UTF8);
			if (git_repository_open(&repository, gitdir))
			{
				CMessageBox::Show(m_hWnd, CGit::GetLibGit2LastErr(_T("Could not open repository.")), _T("TortoiseGit"), MB_OK | MB_ICONERROR);
				break;
			}

			CGitHash revHash;
			CString revRef = _T("HEAD");
			if (m_bCommitAmend && !m_bAmendDiffToLastCommit)
				revRef = _T("HEAD~1");
			if (g_Git.GetHash(revHash, revRef))
			{
				git_repository_free(repository);
				MessageBox(g_Git.GetLibGit2LastErr(_T("Could not get HEAD hash after committing.")), _T("TortoiseGit"), MB_ICONERROR);
				break;
			}

			git_commit *commit = nullptr;
			if (!revHash.IsEmpty() && needResetIndex && git_commit_lookup(&commit, repository, (const git_oid*)revHash.m_hash))
			{
				git_repository_free(repository);
				CMessageBox::Show(m_hWnd, CGit::GetLibGit2LastErr(_T("Could not get last commit.")), _T("TortoiseGit"), MB_OK | MB_ICONERROR);
				break;
			}

			git_tree *tree = nullptr;
			if (!revHash.IsEmpty() && needResetIndex && git_commit_tree(&tree, commit))
			{
				git_commit_free(commit);
				git_repository_free(repository);
				CMessageBox::Show(m_hWnd, CGit::GetLibGit2LastErr(_T("Could not read tree of commit.")), _T("TortoiseGit"), MB_OK | MB_ICONERROR);
				break;
			}

			git_index *index = nullptr;
			if (git_repository_index(&index, repository))
			{
				if (tree != nullptr)
					git_tree_free(tree);
				if (commit != nullptr)
					git_commit_free(commit);
				git_repository_free(repository);
				CMessageBox::Show(m_hWnd, CGit::GetLibGit2LastErr(_T("Could not get the repository index.")), _T("TortoiseGit"), MB_OK | MB_ICONERROR);
				break;
			}

			// reset index to the one of the reference commit (HEAD or HEAD~1)
			if (!revHash.IsEmpty() && needResetIndex && git_index_read_tree(index, tree))
			{
				git_index_free(index);
				git_tree_free(tree);
				git_commit_free(commit);
				git_repository_free(repository);
				CMessageBox::Show(m_hWnd, CGit::GetLibGit2LastErr(_T("Could not read the tree into the index.")), _T("TortoiseGit"), MB_OK | MB_ICONERROR);
				break;
			}
			else if (!revHash.IsEmpty() && !needResetIndex)
			{
				git_index_read(index, true);
			}

			bAddSuccess = true;

			for (int j = 0; j < nListItems; ++j)
			{
				CTGitPath *entry = (CTGitPath*)m_ListCtrl.GetItemData(j);

				if (sysProgressDlg.IsVisible())
				{
					if (GetTickCount() - currentTicks > 1000 || j == nListItems - 1 || j == 0)
					{
						sysProgressDlg.SetLine(2, entry->GetGitPathString(), true);
						sysProgressDlg.SetProgress(j, nListItems);
						AfxGetThread()->PumpMessage(); // process messages, in order to avoid freezing; do not call this too often: this takes time!
						currentTicks = GetTickCount();
					}
				}

				CStringA filePathA = CUnicodeUtils::GetMulti(entry->GetGitPathString(), CP_UTF8);

				if (entry->m_Checked && !m_bCommitMessageOnly)
				{
					if (entry->IsDirectory())
					{
						git_submodule *submodule = nullptr;
						if (git_submodule_lookup(&submodule, repository, filePathA))
						{
							bAddSuccess = false;
							CMessageBox::Show(m_hWnd, CGit::GetLibGit2LastErr(_T("Could not open submodule \"") + entry->GetGitPathString() + _T("\".")), _T("TortoiseGit"), MB_OK | MB_ICONERROR);
							break;
						}
						if (git_submodule_add_to_index(submodule, FALSE))
						{
							bAddSuccess = false;
							CMessageBox::Show(m_hWnd, CGit::GetLibGit2LastErr(_T("Could not add submodule \"") + entry->GetGitPathString() + _T("\" to index.")), _T("TortoiseGit"), MB_OK | MB_ICONERROR);
							break;
						}
					}
					else if (entry->m_Action & CTGitPath::LOGACTIONS_DELETED)
					{
						git_index_remove_bypath(index, filePathA); // ignore error
					}
					else
					{
						if (git_index_add_bypath(index, filePathA))
						{
							bAddSuccess = false;
							CMessageBox::Show(m_hWnd, CGit::GetLibGit2LastErr(_T("Could not add \"") + entry->GetGitPathString() + _T("\" to index.")), _T("TortoiseGit"), MB_OK | MB_ICONERROR);
							break;
						}
					}

					if (entry->m_Action & CTGitPath::LOGACTIONS_REPLACED)
					{
						git_index_remove_bypath(index, filePathA); // ignore error
					}

					++nchecked;
				}
				else if (entry->m_Action & CTGitPath::LOGACTIONS_ADDED || entry->m_Action & CTGitPath::LOGACTIONS_REPLACED)
					mgtReAddAfterCommit.AddFile(*entry);

				if (sysProgressDlg.HasUserCancelled())
				{
					bAddSuccess = false;
					break;
				}

				CShellUpdater::Instance().AddPathForUpdate(*entry);
			}
			if (bAddSuccess && git_index_write(index))
				bAddSuccess = false;

			git_index_free(index);
			if (tree != nullptr)
				git_tree_free(tree);
			if (commit != nullptr)
				git_commit_free(commit);
			git_repository_free(repository);
		} while (0);
	}
	else
	{
		// ***************************************************
		// ATTENTION: Similar code in RebaseDlg.cpp!!!
		// ***************************************************
		CMassiveGitTask mgtAdd(_T("add -f"));
		CMassiveGitTask mgtUpdateIndexForceRemove(_T("update-index --force-remove"));
		CMassiveGitTask mgtUpdateIndex(_T("update-index"));
		CMassiveGitTask mgtRm(_T("rm --ignore-unmatch"));
		CMassiveGitTask mgtRmFCache(_T("rm -f --cache"));
		CString resetCmd = _T("reset");
		if (m_bCommitAmend && !m_bAmendDiffToLastCommit)
			resetCmd += _T(" HEAD~1");;
		CMassiveGitTask mgtReset(resetCmd, TRUE, true);
		for (int j = 0; j < nListItems; ++j)
		{
			CTGitPath *entry = (CTGitPath*)m_ListCtrl.GetItemData(j);

			if (entry->m_Checked && !m_bCommitMessageOnly)
			{
				if (entry->m_Action & CTGitPath::LOGACTIONS_UNVER)
					mgtAdd.AddFile(entry->GetGitPathString());
				else if (entry->m_Action & CTGitPath::LOGACTIONS_DELETED)
					mgtUpdateIndexForceRemove.AddFile(entry->GetGitPathString());
				else
					mgtUpdateIndex.AddFile(entry->GetGitPathString());

				if ((entry->m_Action & CTGitPath::LOGACTIONS_REPLACED) && !entry->GetGitOldPathString().IsEmpty())
					mgtRm.AddFile(entry->GetGitOldPathString());

				++nchecked;
			}
			else
			{
				if (entry->m_Action & CTGitPath::LOGACTIONS_ADDED || entry->m_Action & CTGitPath::LOGACTIONS_REPLACED)
				{	//To init git repository, there are not HEAD, so we can use git reset command
					mgtRmFCache.AddFile(entry->GetGitPathString());
					mgtReAddAfterCommit.AddFile(*entry);

					if (entry->m_Action & CTGitPath::LOGACTIONS_REPLACED && !entry->GetGitOldPathString().IsEmpty())
						mgtReset.AddFile(entry->GetGitOldPathString());
				}
				else if (!(entry->m_Action & CTGitPath::LOGACTIONS_UNVER))
					mgtReset.AddFile(entry->GetGitPathString());
			}

			if (sysProgressDlg.HasUserCancelled())
			{
				bAddSuccess = false;
				break;
			}
		}

		CMassiveGitTask tasks[] = { mgtAdd, mgtUpdateIndexForceRemove, mgtUpdateIndex, mgtRm, mgtRmFCache, mgtReset };
		int progress = 0, maxProgress = 0;
		for (int j = 0; j < _countof(tasks); ++j)
			maxProgress += tasks[j].GetListCount();
		for (int j = 0; j < _countof(tasks); ++j)
			bAddSuccess = bAddSuccess && UpdateIndex(tasks[j], sysProgressDlg, progress += tasks[j].GetListCount(), maxProgress);

		if (sysProgressDlg.HasUserCancelled())
			bAddSuccess = false;

		for (int j = 0; bAddSuccess && j < nListItems; ++j)
			CShellUpdater::Instance().AddPathForUpdate(*(CTGitPath*)m_ListCtrl.GetItemData(j));
	}

	if (sysProgressDlg.HasUserCancelled())
		bAddSuccess = false;

	//sysProgressDlg.Stop();

	if (bAddSuccess && m_bCreateNewBranch)
	{
		if (g_Git.Run(_T("git.exe branch ") + m_sCreateNewBranch, &out, CP_UTF8))
		{
			MessageBox(_T("Creating new branch failed:\n") + out, _T("TortoiseGit"), MB_OK | MB_ICONERROR);
			bAddSuccess = false;
		}
		if (g_Git.Run(_T("git.exe checkout ") + m_sCreateNewBranch + _T(" --"), &out, CP_UTF8))
		{
			MessageBox(_T("Switching to new branch failed:\n") + out, _T("TortoiseGit"), MB_OK | MB_ICONERROR);
			bAddSuccess = false;
		}
	}

	if (bAddSuccess && CheckHeadDetach())
		bAddSuccess = false;

	m_sBugID.Trim();
	CString sExistingBugID = m_ProjectProperties.FindBugID(m_sLogMessage);
	sExistingBugID.Trim();
	if (!m_sBugID.IsEmpty() && m_sBugID.Compare(sExistingBugID))
	{
		m_sBugID.Replace(_T(", "), _T(","));
		m_sBugID.Replace(_T(" ,"), _T(","));
		CString sBugID = m_ProjectProperties.sMessage;
		sBugID.Replace(_T("%BUGID%"), m_sBugID);
		if (m_ProjectProperties.bAppend)
			m_sLogMessage += _T("\n") + sBugID + _T("\n");
		else
			m_sLogMessage = sBugID + _T("\n") + m_sLogMessage;
	}

	// now let the bugtraq plugin check the commit message
	CComPtr<IBugTraqProvider2> pProvider2 = NULL;
	if (m_BugTraqProvider)
	{
		HRESULT hr = m_BugTraqProvider.QueryInterface(&pProvider2);
		if (SUCCEEDED(hr))
		{
			BSTR temp = NULL;
			CString common = g_Git.m_CurrentDir;
			BSTR repositoryRoot = common.AllocSysString();
			BSTR parameters = m_bugtraq_association.GetParameters().AllocSysString();
			BSTR commonRoot = SysAllocString(m_pathList.GetCommonRoot().GetDirectory().GetWinPath());
			BSTR commitMessage = m_sLogMessage.AllocSysString();
			SAFEARRAY *pathList = SafeArrayCreateVector(VT_BSTR, 0, m_selectedPathList.GetCount());

			for (LONG index = 0; index < m_selectedPathList.GetCount(); ++index)
				SafeArrayPutElement(pathList, &index, m_selectedPathList[index].GetGitPathString().AllocSysString());

			if (FAILED(hr = pProvider2->CheckCommit(GetSafeHwnd(), parameters, repositoryRoot, commonRoot, pathList, commitMessage, &temp)))
			{
				COMError ce(hr);
				CString sErr;
				sErr.Format(IDS_ERR_FAILEDISSUETRACKERCOM, m_bugtraq_association.GetProviderName(), ce.GetMessageAndDescription().c_str());
				CMessageBox::Show(m_hWnd, sErr, _T("TortoiseGit"), MB_ICONERROR);
			}
			else
			{
				CString sError = temp;
				if (!sError.IsEmpty())
				{
					CMessageBox::Show(m_hWnd, sError, _T("TortoiseGit"), MB_ICONERROR);
					return;
				}
				SysFreeString(temp);
			}
		}
	}

	if (m_bCommitMessageOnly || bAddSuccess && (nchecked || m_bCommitAmend ||  CTGitPath(g_Git.m_CurrentDir).IsMergeActive()))
	{
		bCloseCommitDlg = true;

		CString tempfile=::GetTempFile();

		CAppUtils::SaveCommitUnicodeFile(tempfile,m_sLogMessage);

		CTGitPath path=g_Git.m_CurrentDir;

		BOOL IsGitSVN = path.GetAdminDirMask() & ITEMIS_GITSVN;

		out =_T("");
		CString amend;
		if(this->m_bCommitAmend)
		{
			amend=_T("--amend");
		}
		CString dateTime;
		if (m_bSetCommitDateTime)
		{
			CTime date, time;
			m_CommitDate.GetTime(date);
			m_CommitTime.GetTime(time);
			dateTime.Format(_T("--date=%sT%s"), date.Format(_T("%Y-%m-%d")), time.Format(_T("%H:%M:%S")));
		}
		CString author;
		if (m_bSetAuthor)
			author.Format(_T("--author=\"%s\""), m_sAuthor);
		CString allowEmpty = m_bCommitMessageOnly ? _T("--allow-empty") : _T("");
		cmd.Format(_T("git.exe commit %s %s %s %s -F \"%s\""), author, dateTime, amend, allowEmpty, tempfile);

		CCommitProgressDlg progress;
		progress.m_bBufferAll=true; // improve show speed when there are many file added.
		progress.m_GitCmd=cmd;
		progress.m_bShowCommand = FALSE;	// don't show the commit command
		progress.m_PreText = out;			// show any output already generated in log window

		int indexPull = -1;
		int indexReCommit = -1;
		int indexTag = -1;

		if (!m_bNoPostActions && !m_bAutoClose)
		{
			if (IsGitSVN)
				progress.m_PostCmdList.Add(CString(MAKEINTRESOURCE(IDS_MENUSVNDCOMMIT)));
			progress.m_PostCmdList.Add(CString(MAKEINTRESOURCE(IDS_MENUPUSH)));
			indexPull = (int)progress.m_PostCmdList.Add(CString(MAKEINTRESOURCE(IDS_MENUPULL)));
			indexReCommit = (int)progress.m_PostCmdList.Add(CString(MAKEINTRESOURCE(IDS_PROC_COMMIT_RECOMMIT)));
			indexTag = (int)progress.m_PostCmdList.Add(CString(MAKEINTRESOURCE(IDS_MENUTAG)));
		}

		INT_PTR userResponse = progress.DoModal();

		m_PostCmd = GIT_POSTCOMMIT_CMD_NOTHING;
		if(progress.m_GitStatus || userResponse == (IDC_PROGRESS_BUTTON1 + indexReCommit))
		{
			bCloseCommitDlg = false;
			if (userResponse == IDC_PROGRESS_BUTTON1 + indexReCommit)
			{
				this->m_sLogMessage.Empty();
				m_cLogMessage.SetText(m_sLogMessage);
				if (m_bCreateNewBranch)
				{
					GetDlgItem(IDC_COMMIT_TO)->ShowWindow(SW_SHOW);
					GetDlgItem(IDC_NEWBRANCH)->ShowWindow(SW_HIDE);
				}
				m_bCreateNewBranch = FALSE;
			}

			m_bCommitAmend = FALSE;
			UpdateData(FALSE);
			this->Refresh();
			this->BringWindowToTop();
		}
		else if (userResponse == IDC_PROGRESS_BUTTON1 + indexTag)
			m_PostCmd = GIT_POSTCOMMIT_CMD_CREATETAG;
		else if (userResponse == IDC_PROGRESS_BUTTON1 + indexPull)
			m_PostCmd = GIT_POSTCOMMIT_CMD_PULL;
		else if (userResponse >= IDC_PROGRESS_BUTTON1 && userResponse < IDC_PROGRESS_BUTTON1 + indexPull)
		{
			// User pressed 'DCommit' or 'Push' button after successful commit.
			if (userResponse == IDC_PROGRESS_BUTTON1 && IsGitSVN)
				m_PostCmd = GIT_POSTCOMMIT_CMD_DCOMMIT;
			else
				m_PostCmd = GIT_POSTCOMMIT_CMD_PUSH;
		}

		::DeleteFile(tempfile);

		if (m_BugTraqProvider && progress.m_GitStatus == 0)
		{
			CComPtr<IBugTraqProvider2> pProvider = NULL;
			HRESULT hr = m_BugTraqProvider.QueryInterface(&pProvider);
			if (SUCCEEDED(hr))
			{
				BSTR commonRoot = SysAllocString(g_Git.m_CurrentDir);
				SAFEARRAY *pathList = SafeArrayCreateVector(VT_BSTR, 0,this->m_selectedPathList.GetCount());

				for (LONG index = 0; index < m_selectedPathList.GetCount(); ++index)
					SafeArrayPutElement(pathList, &index, m_selectedPathList[index].GetGitPathString().AllocSysString());

				BSTR logMessage = m_sLogMessage.AllocSysString();

				CGitHash hash;
				if (g_Git.GetHash(hash, _T("HEAD")))
					MessageBox(g_Git.GetGitLastErr(_T("Could not get HEAD hash after committing.")), _T("TortoiseGit"), MB_ICONERROR);
				LONG version = g_Git.Hash2int(hash);

				BSTR temp = NULL;
				if (FAILED(hr = pProvider->OnCommitFinished(GetSafeHwnd(),
					commonRoot,
					pathList,
					logMessage,
					(LONG)version,
					&temp)))
				{
					CString sErr = temp;
					if (!sErr.IsEmpty())
						CMessageBox::Show(NULL,(sErr),_T("TortoiseGit"),MB_OK|MB_ICONERROR);
					else
					{
						COMError ce(hr);
						sErr.Format(IDS_ERR_FAILEDISSUETRACKERCOM, ce.GetSource().c_str(), ce.GetMessageAndDescription().c_str());
						CMessageBox::Show(NULL,(sErr),_T("TortoiseGit"),MB_OK|MB_ICONERROR);
					}
				}

				SysFreeString(temp);
			}
		}
		RestoreFiles(progress.m_GitStatus == 0);
		if (((DWORD)CRegStdDWORD(_T("Software\\TortoiseGit\\ReaddUnselectedAddedFilesAfterCommit"), TRUE)) == TRUE)
		{
			BOOL cancel = FALSE;
			mgtReAddAfterCommit.Execute(cancel);
		}
	}
	else if(bAddSuccess)
	{
		CMessageBox::Show(this->m_hWnd, IDS_ERROR_NOTHING_COMMIT, IDS_COMMIT_FINISH, MB_OK | MB_ICONINFORMATION);
		bCloseCommitDlg=false;
	}

	UpdateData();
	m_regAddBeforeCommit = m_bShowUnversioned;
	m_regKeepChangelists = m_bKeepChangeList;
	m_regDoNotAutoselectSubmodules = m_bDoNotAutoselectSubmodules;
	if (!GetDlgItem(IDC_KEEPLISTS)->IsWindowEnabled())
		m_bKeepChangeList = FALSE;
	InterlockedExchange(&m_bBlock, FALSE);

	if (!m_sLogMessage.IsEmpty())
	{
		m_History.AddEntry(m_sLogMessage);
		m_History.Save();
	}

	SaveSplitterPos();

	if( bCloseCommitDlg )
		CResizableStandAloneDialog::OnOK();

	CShellUpdater::Instance().Flush();
}

void CCommitDlg::SaveSplitterPos()
{
	if (!IsIconic())
	{
		CRegDWORD regPos = CRegDWORD(_T("Software\\TortoiseGit\\TortoiseProc\\ResizableState\\CommitDlgSizer"));
		RECT rectSplitter;
		m_wndSplitter.GetWindowRect(&rectSplitter);
		ScreenToClient(&rectSplitter);
		regPos = rectSplitter.top;
	}
}

UINT CCommitDlg::StatusThreadEntry(LPVOID pVoid)
{
	return ((CCommitDlg*)pVoid)->StatusThread();
}

UINT CCommitDlg::StatusThread()
{
	//get the status of all selected file/folders recursively
	//and show the ones which have to be committed to the user
	//in a list control.
	InterlockedExchange(&m_bBlock, TRUE);
	InterlockedExchange(&m_bThreadRunning, TRUE);// so the main thread knows that this thread is still running
	InterlockedExchange(&m_bRunThread, TRUE);	// if this is set to FALSE, the thread should stop

	m_pathwatcher.Stop();

	m_ListCtrl.SetBusy(true);
	g_Git.RefreshGitIndex();

	m_bCancelled = false;

	DialogEnableWindow(IDOK, false);
	DialogEnableWindow(IDC_SHOWUNVERSIONED, false);
	DialogEnableWindow(IDC_WHOLE_PROJECT, false);
	DialogEnableWindow(IDC_NOAUTOSELECTSUBMODULES, false);
	GetDlgItem(IDC_EXTERNALWARNING)->ShowWindow(SW_HIDE);
	DialogEnableWindow(IDC_EXTERNALWARNING, false);
	DialogEnableWindow(IDC_COMMIT_AMEND, FALSE);
	DialogEnableWindow(IDC_COMMIT_AMENDDIFF, FALSE);
	// read the list of recent log entries before querying the WC for status
	// -> the user may select one and modify / update it while we are crawling the WC

	DialogEnableWindow(IDC_CHECKALL, false);
	DialogEnableWindow(IDC_CHECKNONE, false);
	DialogEnableWindow(IDC_CHECKUNVERSIONED, false);
	DialogEnableWindow(IDC_CHECKVERSIONED, false);
	DialogEnableWindow(IDC_CHECKADDED, false);
	DialogEnableWindow(IDC_CHECKDELETED, false);
	DialogEnableWindow(IDC_CHECKMODIFIED, false);
	DialogEnableWindow(IDC_CHECKFILES, false);
	DialogEnableWindow(IDC_CHECKSUBMODULES, false);

	if (m_History.IsEmpty())
	{
		CString reg;
		reg.Format(_T("Software\\TortoiseGit\\History\\commit%s"), (LPCTSTR)m_ListCtrl.m_sUUID);
		reg.Replace(_T(':'),_T('_'));
		m_History.Load(reg, _T("logmsgs"));
	}

	// Initialise the list control with the status of the files/folders below us
	m_ListCtrl.Clear();
	BOOL success;
	CTGitPathList *pList;
	m_ListCtrl.m_amend = (m_bCommitAmend==TRUE || m_bForceCommitAmend) && (m_bAmendDiffToLastCommit==FALSE);
	m_ListCtrl.m_bDoNotAutoselectSubmodules = (m_bDoNotAutoselectSubmodules == TRUE);

	if(m_bWholeProject)
		pList=NULL;
	else
		pList = &m_pathList;

	success=m_ListCtrl.GetStatus(pList);

	//m_ListCtrl.UpdateFileList(git_revnum_t(GIT_REV_ZERO));
	if(this->m_bShowUnversioned)
		m_ListCtrl.UpdateFileList(CGitStatusListCtrl::FILELIST_UNVER,true,pList);

	m_ListCtrl.CheckIfChangelistsArePresent(false);

	DWORD dwShow = GITSLC_SHOWVERSIONEDBUTNORMALANDEXTERNALSFROMDIFFERENTREPOS | GITSLC_SHOWLOCKS | GITSLC_SHOWINCHANGELIST | GITSLC_SHOWDIRECTFILES;
	dwShow |= DWORD(m_regAddBeforeCommit) ? GITSLC_SHOWUNVERSIONED : 0;
	if (success)
	{
		if (!m_checkedPathList.IsEmpty())
			m_ListCtrl.Show(dwShow, m_checkedPathList);
		else
		{
			DWORD dwCheck = m_bSelectFilesForCommit ? dwShow : 0;
			dwCheck &=~(CTGitPath::LOGACTIONS_UNVER); //don't check unversion file default.
			m_ListCtrl.Show(dwShow, dwCheck);
			m_bSelectFilesForCommit = true;
		}

		if (m_ListCtrl.HasExternalsFromDifferentRepos())
		{
			GetDlgItem(IDC_EXTERNALWARNING)->ShowWindow(SW_SHOW);
			DialogEnableWindow(IDC_EXTERNALWARNING, TRUE);
		}

		SetDlgItemText(IDC_COMMIT_TO, g_Git.GetCurrentBranch());
		m_tooltips.AddTool(GetDlgItem(IDC_STATISTICS), m_ListCtrl.GetStatisticsString());
	}
	if (!success)
	{
		if (!m_ListCtrl.GetLastErrorMessage().IsEmpty())
			m_ListCtrl.SetEmptyString(m_ListCtrl.GetLastErrorMessage());
		m_ListCtrl.Show(dwShow);
	}

	CString dotGitPath;
	g_GitAdminDir.GetAdminDirPath(g_Git.m_CurrentDir, dotGitPath);
	if ((m_ListCtrl.GetItemCount()==0)&&(m_ListCtrl.HasUnversionedItems())
		 && !PathFileExists(dotGitPath + _T("MERGE_HEAD")))
	{
		CString temp;
		temp.LoadString(IDS_COMMITDLG_NOTHINGTOCOMMITUNVERSIONED);
		if (CMessageBox::ShowCheck(m_hWnd, temp, _T("TortoiseGit"), MB_ICONINFORMATION | MB_YESNO, _T("NothingToCommitShowUnversioned"), NULL)==IDYES)
		{
			m_bShowUnversioned = TRUE;
			GetDlgItem(IDC_SHOWUNVERSIONED)->SendMessage(BM_SETCHECK, BST_CHECKED);
			DWORD dwShow = (DWORD)(GITSLC_SHOWVERSIONEDBUTNORMALANDEXTERNALSFROMDIFFERENTREPOS | GITSLC_SHOWUNVERSIONED | GITSLC_SHOWLOCKS);
			m_ListCtrl.UpdateFileList(CGitStatusListCtrl::FILELIST_UNVER);
			m_ListCtrl.Show(dwShow,dwShow&(~CTGitPath::LOGACTIONS_UNVER));
		}
	}

	SetDlgTitle();

	m_autolist.clear();
	// we don't have to block the commit dialog while we fetch the
	// auto completion list.
	m_pathwatcher.ClearChangedPaths();
	InterlockedExchange(&m_bBlock, FALSE);
	if ((DWORD)CRegDWORD(_T("Software\\TortoiseGit\\Autocompletion"), TRUE)==TRUE)
	{
		m_ListCtrl.Block(TRUE, TRUE);
		GetAutocompletionList();
		m_ListCtrl.Block(FALSE, FALSE);
	}
	SendMessage(WM_UPDATEOKBUTTON);
	if (m_bRunThread)
	{
		DialogEnableWindow(IDC_SHOWUNVERSIONED, true);
		DialogEnableWindow(IDC_WHOLE_PROJECT, !(m_pathList.IsEmpty() || (m_pathList.GetCount() == 1 && m_pathList[0].IsEmpty())));
		DialogEnableWindow(IDC_NOAUTOSELECTSUBMODULES, true);
		if (m_ListCtrl.HasChangeLists())
			DialogEnableWindow(IDC_KEEPLISTS, true);

		// activate amend checkbox (if necessary)
		if (g_Git.IsInitRepos())
		{
			m_bCommitAmend = FALSE;
			SendMessage(WM_UPDATEDATAFALSE);
		}
		else
		{
			if (m_bForceCommitAmend)
			{
				GetDlgItem(IDC_COMMIT_AMENDDIFF)->ShowWindow(SW_SHOW);
				m_bCommitAmend = TRUE;
				SendMessage(WM_UPDATEDATAFALSE);
			}
			else
				GetDlgItem(IDC_COMMIT_AMEND)->EnableWindow(TRUE);

			CGitHash hash;
			if (g_Git.GetHash(hash, _T("HEAD")))
			{
				MessageBox(g_Git.GetGitLastErr(_T("Could not get HEAD hash.")), _T("TortoiseGit"), MB_ICONERROR);
			}
			if (!hash.IsEmpty())
			{
				GitRev headRevision;
				try
				{
					headRevision.GetParentFromHash(hash);
				}
				catch (char* msg)
				{
					CString err(msg);
					MessageBox(_T("Could not get parent from HEAD.\nlibgit reports:\n") + err, _T("TortoiseGit"), MB_ICONERROR);
				}
				// do not allow to show diff to "last" revision if it has more that one parent
				if (headRevision.ParentsCount() != 1)
				{
					m_bAmendDiffToLastCommit = TRUE;
					SendMessage(WM_UPDATEDATAFALSE);
				}
				else
					GetDlgItem(IDC_COMMIT_AMENDDIFF)->EnableWindow(TRUE);
			}
		}

		UpdateCheckLinks();

		// we have the list, now signal the main thread about it
		SendMessage(WM_AUTOLISTREADY);	// only send the message if the thread wasn't told to quit!
	}

	InterlockedExchange(&m_bRunThread, FALSE);
	InterlockedExchange(&m_bThreadRunning, FALSE);
	// force the cursor to normal
	RefreshCursor();

	return 0;
}

void CCommitDlg::SetDlgTitle()
{
	if (m_sTitle.IsEmpty())
		GetWindowText(m_sTitle);

	if (m_bWholeProject)
		CAppUtils::SetWindowTitle(m_hWnd, g_Git.m_CurrentDir, m_sTitle);
	else
	{
		if (m_pathList.GetCount() == 1)
			CAppUtils::SetWindowTitle(m_hWnd, (g_Git.m_CurrentDir + _T("\\") + m_pathList[0].GetUIPathString()).TrimRight('\\'), m_sTitle);
		else
			CAppUtils::SetWindowTitle(m_hWnd, g_Git.m_CurrentDir + _T("\\") + m_ListCtrl.GetCommonDirectory(false), m_sTitle);
	}
}

void CCommitDlg::OnCancel()
{
	m_bCancelled = true;
	m_pathwatcher.Stop();

	if (m_bThreadRunning)
	{
		InterlockedExchange(&m_bRunThread, FALSE);
		WaitForSingleObject(m_pThread->m_hThread, 1000);
		if (m_bThreadRunning)
		{
			// we gave the thread a chance to quit. Since the thread didn't
			// listen to us we have to kill it.
			TerminateThread(m_pThread->m_hThread, (DWORD)-1);
			InterlockedExchange(&m_bThreadRunning, FALSE);
		}
	}
	UpdateData();
	m_sBugID.Trim();
	m_sLogMessage = m_cLogMessage.GetText();
	if (!m_sBugID.IsEmpty())
	{
		m_sBugID.Replace(_T(", "), _T(","));
		m_sBugID.Replace(_T(" ,"), _T(","));
		CString sBugID = m_ProjectProperties.sMessage;
		sBugID.Replace(_T("%BUGID%"), m_sBugID);
		if (m_ProjectProperties.bAppend)
			m_sLogMessage += _T("\n") + sBugID + _T("\n");
		else
			m_sLogMessage = sBugID + _T("\n") + m_sLogMessage;
	}
	if ((m_sLogTemplate.Compare(m_sLogMessage) != 0) && !m_sLogMessage.IsEmpty())
	{
		m_History.AddEntry(m_sLogMessage);
		m_History.Save();
	}
	RestoreFiles();
	SaveSplitterPos();
	CResizableStandAloneDialog::OnCancel();
}

BOOL CCommitDlg::PreTranslateMessage(MSG* pMsg)
{
	if (!m_bBlock)
		m_tooltips.RelayEvent(pMsg);

	if (m_hAccel)
	{
		int ret = TranslateAccelerator(m_hWnd, m_hAccel, pMsg);
		if (ret)
			return TRUE;
	}

	if (pMsg->message == WM_KEYDOWN)
	{
		switch (pMsg->wParam)
		{
		case VK_F5:
			{
				if (m_bBlock)
					return CResizableStandAloneDialog::PreTranslateMessage(pMsg);
				Refresh();
			}
			break;
		case VK_RETURN:
			{
				if (GetAsyncKeyState(VK_CONTROL)&0x8000)
				{
					if ( GetDlgItem(IDOK)->IsWindowEnabled() )
					{
						PostMessage(WM_COMMAND, IDOK);
					}
					return TRUE;
				}
				if ( GetFocus()==GetDlgItem(IDC_BUGID) )
				{
					// Pressing RETURN in the bug id control
					// moves the focus to the message
					GetDlgItem(IDC_LOGMESSAGE)->SetFocus();
					return TRUE;
				}
			}
			break;
		}
	}

	return CResizableStandAloneDialog::PreTranslateMessage(pMsg);
}

void CCommitDlg::Refresh()
{
	if (m_bThreadRunning)
		return;

	InterlockedExchange(&m_bBlock, TRUE);
	m_pThread = AfxBeginThread(StatusThreadEntry, this, THREAD_PRIORITY_NORMAL,0,CREATE_SUSPENDED);
	if (m_pThread==NULL)
	{
		CMessageBox::Show(this->m_hWnd, IDS_ERR_THREADSTARTFAILED, IDS_APPNAME, MB_OK | MB_ICONERROR);
		InterlockedExchange(&m_bBlock, FALSE);
	}
	else
	{
		m_pThread->m_bAutoDelete = FALSE;
		m_pThread->ResumeThread();
	}
}

void CCommitDlg::OnBnClickedShowunversioned()
{
	m_tooltips.Pop();	// hide the tooltips
	UpdateData();
	m_regAddBeforeCommit = m_bShowUnversioned;
	if (!m_bBlock)
	{
		DWORD dwShow = m_ListCtrl.GetShowFlags();
		if (DWORD(m_regAddBeforeCommit))
			dwShow |= GITSLC_SHOWUNVERSIONED;
		else
			dwShow &= ~GITSLC_SHOWUNVERSIONED;
		if(dwShow & GITSLC_SHOWUNVERSIONED)
		{
			if(m_bWholeProject)
				m_ListCtrl.GetStatus(NULL,false,false,true);
			else
				m_ListCtrl.GetStatus(&this->m_pathList,false,false,true);
		}
		m_ListCtrl.Show(dwShow, 0, true, dwShow & ~(CTGitPath::LOGACTIONS_UNVER), true);
		UpdateCheckLinks();
	}
}

void CCommitDlg::OnStnClickedExternalwarning()
{
	m_tooltips.Popup();
}

void CCommitDlg::OnEnChangeLogmessage()
{
	SendMessage(WM_UPDATEOKBUTTON);
}

LRESULT CCommitDlg::OnGitStatusListCtrlItemCountChanged(WPARAM, LPARAM)
{
#if 0
	if ((m_ListCtrl.GetItemCount() == 0)&&(m_ListCtrl.HasUnversionedItems())&&(!m_bShowUnversioned))
	{
		if (CMessageBox::Show(*this, IDS_COMMITDLG_NOTHINGTOCOMMITUNVERSIONED, IDS_APPNAME, MB_ICONINFORMATION | MB_YESNO)==IDYES)
		{
			m_bShowUnversioned = TRUE;
			DWORD dwShow = GitSLC_SHOWVERSIONEDBUTNORMALANDEXTERNALSFROMDIFFERENTREPOS | GitSLC_SHOWUNVERSIONED | GitSLC_SHOWLOCKS;
			m_ListCtrl.Show(dwShow);
			UpdateData(FALSE);
		}
	}
#endif
	return 0;
}

LRESULT CCommitDlg::OnGitStatusListCtrlNeedsRefresh(WPARAM, LPARAM)
{
	Refresh();
	return 0;
}

LRESULT CCommitDlg::OnFileDropped(WPARAM, LPARAM /*lParam*/)
{
#if 0
	BringWindowToTop();
	SetForegroundWindow();
	SetActiveWindow();
	// if multiple files/folders are dropped
	// this handler is called for every single item
	// separately.
	// To avoid creating multiple refresh threads and
	// causing crashes, we only add the items to the
	// list control and start a timer.
	// When the timer expires, we start the refresh thread,
	// but only if it isn't already running - otherwise we
	// restart the timer.
	CTGitPath path;
	path.SetFromWin((LPCTSTR)lParam);

	// just add all the items we get here.
	// if the item is versioned, the add will fail but nothing
	// more will happen.
	Git Git;
	Git.Add(CTGitPathList(path), &m_ProjectProperties, Git_depth_empty, false, true, true);

	if (!m_ListCtrl.HasPath(path))
	{
		if (m_pathList.AreAllPathsFiles())
		{
			m_pathList.AddPath(path);
			m_pathList.RemoveDuplicates();
			m_updatedPathList.AddPath(path);
			m_updatedPathList.RemoveDuplicates();
		}
		else
		{
			// if the path list contains folders, we have to check whether
			// our just (maybe) added path is a child of one of those. If it is
			// a child of a folder already in the list, we must not add it. Otherwise
			// that path could show up twice in the list.
			bool bHasParentInList = false;
			for (int i=0; i<m_pathList.GetCount(); ++i)
			{
				if (m_pathList[i].IsAncestorOf(path))
				{
					bHasParentInList = true;
					break;
				}
			}
			if (!bHasParentInList)
			{
				m_pathList.AddPath(path);
				m_pathList.RemoveDuplicates();
				m_updatedPathList.AddPath(path);
				m_updatedPathList.RemoveDuplicates();
			}
		}
	}

	// Always start the timer, since the status of an existing item might have changed
	SetTimer(REFRESHTIMER, 200, NULL);
	CTraceToOutputDebugString::Instance()(_T(__FUNCTION__) _T(": Item %s dropped, timer started\n"), path.GetWinPath());
#endif
	return 0;
}

LRESULT CCommitDlg::OnAutoListReady(WPARAM, LPARAM)
{
	m_cLogMessage.SetAutoCompletionList(m_autolist, '*');
	return 0;
}

//////////////////////////////////////////////////////////////////////////
// functions which run in the status thread
//////////////////////////////////////////////////////////////////////////

void CCommitDlg::ParseRegexFile(const CString& sFile, std::map<CString, CString>& mapRegex)
{
	CString strLine;
	try
	{
		CStdioFile file(sFile, CFile::typeText | CFile::modeRead | CFile::shareDenyWrite);
		while (m_bRunThread && file.ReadString(strLine))
		{
			int eqpos = strLine.Find('=');
			CString rgx;
			rgx = strLine.Mid(eqpos+1).Trim();

			int pos = -1;
			while (((pos = strLine.Find(','))>=0)&&(pos < eqpos))
			{
				mapRegex[strLine.Left(pos)] = rgx;
				strLine = strLine.Mid(pos+1).Trim();
			}
			mapRegex[strLine.Left(strLine.Find('=')).Trim()] = rgx;
		}
		file.Close();
	}
	catch (CFileException* pE)
	{
		CTraceToOutputDebugString::Instance()(__FUNCTION__ ": CFileException loading auto list regex file\n");
		pE->Delete();
	}
}

void CCommitDlg::GetAutocompletionList()
{
	// the auto completion list is made of strings from each selected files.
	// the strings used are extracted from the files with regexes found
	// in the file "autolist.txt".
	// the format of that file is:
	// file extensions separated with commas '=' regular expression to use
	// example:
	// .h, .hpp = (?<=class[\s])\b\w+\b|(\b\w+(?=[\s ]?\(\);))
	// .cpp = (?<=[^\s]::)\b\w+\b

	std::map<CString, CString> mapRegex;
	CString sRegexFile = CPathUtils::GetAppDirectory();
	CRegDWORD regtimeout = CRegDWORD(_T("Software\\TortoiseGit\\AutocompleteParseTimeout"), 5);
	DWORD timeoutvalue = regtimeout*1000;
	sRegexFile += _T("autolist.txt");
	if (!m_bRunThread)
		return;
	ParseRegexFile(sRegexFile, mapRegex);
	SHGetFolderPath(NULL, CSIDL_APPDATA, NULL, SHGFP_TYPE_CURRENT, sRegexFile.GetBuffer(MAX_PATH+1));
	sRegexFile.ReleaseBuffer();
	sRegexFile += _T("\\TortoiseGit\\autolist.txt");
	if (PathFileExists(sRegexFile))
	{
		ParseRegexFile(sRegexFile, mapRegex);
	}
	DWORD starttime = GetTickCount();

	// now we have two arrays of strings, where the first array contains all
	// file extensions we can use and the second the corresponding regex strings
	// to apply to those files.

	// the next step is to go over all files shown in the commit dialog
	// and scan them for strings we can use
	int nListItems = m_ListCtrl.GetItemCount();

	for (int i=0; i<nListItems && m_bRunThread; ++i)
	{
		// stop parsing after timeout
		if ((!m_bRunThread) || (GetTickCount() - starttime > timeoutvalue))
			return;

		CTGitPath *path = (CTGitPath*)m_ListCtrl.GetItemData(i);

		if(path == NULL)
			continue;

		CString sPartPath =path->GetGitPathString();
		m_autolist.insert(sPartPath);

		int pos = 0;
		int lastPos = 0;
		while ((pos = sPartPath.Find('/', pos)) >= 0)
		{
			++pos;
			lastPos = pos;
			m_autolist.insert(sPartPath.Mid(pos));
		}

		// Last inserted entry is a file name.
		// Some users prefer to also list file name without extension.
		if (CRegDWORD(_T("Software\\TortoiseGit\\AutocompleteRemovesExtensions"), FALSE))
		{
			int dotPos = sPartPath.ReverseFind('.');
			if ((dotPos >= 0) && (dotPos > lastPos))
				m_autolist.insert(sPartPath.Mid(lastPos, dotPos - lastPos));
		}

		if (path->m_Action == CTGitPath::LOGACTIONS_UNVER && !CRegDWORD(_T("Software\\TortoiseGit\\AutocompleteParseUnversioned"), FALSE))
			continue;
		if (path->m_Action == CTGitPath::LOGACTIONS_IGNORE || path->m_Action == CTGitPath::LOGACTIONS_DELETED)
			continue;

		CString sExt = path->GetFileExtension();
		sExt.MakeLower();
		// find the regex string which corresponds to the file extension
		CString rdata = mapRegex[sExt];
		if (rdata.IsEmpty())
			continue;

		ScanFile(path->GetWinPathString(), rdata, sExt);
	}
	CTraceToOutputDebugString::Instance()(_T(__FUNCTION__) _T(": Auto completion list loaded in %d msec\n"), GetTickCount() - starttime);
}

void CCommitDlg::ScanFile(const CString& sFilePath, const CString& sRegex, const CString& sExt)
{
	static std::map<CString, std::tr1::wregex> regexmap;

	std::wstring sFileContent;
	CAutoFile hFile = CreateFile(sFilePath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, NULL, NULL);
	if (hFile)
	{
		DWORD size = GetFileSize(hFile, NULL);
		if (size > CRegDWORD(_T("Software\\TortoiseGit\\AutocompleteParseMaxSize"), 300000L))
		{
			// no files bigger than 300k
			return;
		}
		// allocate memory to hold file contents
		std::unique_ptr<char[]> buffer(new char[size]);
		DWORD readbytes;
		if (!ReadFile(hFile, buffer.get(), size, &readbytes, NULL))
			return;
		int opts = 0;
		IsTextUnicode(buffer.get(), readbytes, &opts);
		if (opts & IS_TEXT_UNICODE_NULL_BYTES)
		{
			return;
		}
		if (opts & IS_TEXT_UNICODE_UNICODE_MASK)
		{
			sFileContent = std::wstring((wchar_t*)buffer.get(), readbytes / sizeof(WCHAR));
		}
		if ((opts & IS_TEXT_UNICODE_NOT_UNICODE_MASK) || (opts == 0))
		{
			const int ret = MultiByteToWideChar(CP_ACP, MB_PRECOMPOSED, (LPCSTR)buffer.get(), readbytes, NULL, 0);
			std::unique_ptr<wchar_t[]> pWideBuf(new wchar_t[ret]);
			const int ret2 = MultiByteToWideChar(CP_ACP, MB_PRECOMPOSED, (LPCSTR)buffer.get(), readbytes, pWideBuf.get(), ret);
			if (ret2 == ret)
				sFileContent = std::wstring(pWideBuf.get(), ret);
		}
	}
	if (sFileContent.empty() || !m_bRunThread)
	{
		return;
	}

	try
	{

		std::tr1::wregex regCheck;
		std::map<CString, std::tr1::wregex>::const_iterator regIt;
		if ((regIt = regexmap.find(sExt)) != regexmap.end())
			regCheck = regIt->second;
		else
		{
			regCheck = std::tr1::wregex(sRegex, std::tr1::regex_constants::icase | std::tr1::regex_constants::ECMAScript);
			regexmap[sExt] = regCheck;
		}
		const std::tr1::wsregex_iterator end;
		for (std::tr1::wsregex_iterator it(sFileContent.begin(), sFileContent.end(), regCheck); it != end; ++it)
		{
			const std::tr1::wsmatch match = *it;
			for (size_t i = 1; i < match.size(); ++i)
			{
				if (match[i].second-match[i].first)
				{
					m_autolist.insert(std::wstring(match[i]).c_str());
				}
			}
		}
	}
	catch (std::exception) {}
}

// CSciEditContextMenuInterface
void CCommitDlg::InsertMenuItems(CMenu& mPopup, int& nCmd)
{
	CString sMenuItemText;
	sMenuItemText.LoadString(IDS_COMMITDLG_POPUP_PICKCOMMITHASH);
	m_nPopupPickCommitHash = nCmd++;
	mPopup.AppendMenu(MF_STRING | MF_ENABLED, m_nPopupPickCommitHash, sMenuItemText);

	sMenuItemText.LoadString(IDS_COMMITDLG_POPUP_PICKCOMMITMESSAGE);
	m_nPopupPickCommitMessage = nCmd++;
	mPopup.AppendMenu(MF_STRING | MF_ENABLED, m_nPopupPickCommitMessage, sMenuItemText);

	sMenuItemText.LoadString(IDS_COMMITDLG_POPUP_PASTEFILELIST);
	m_nPopupPasteListCmd = nCmd++;
	mPopup.AppendMenu(MF_STRING | MF_ENABLED, m_nPopupPasteListCmd, sMenuItemText);

	if (!m_History.IsEmpty())
	{
		sMenuItemText.LoadString(IDS_COMMITDLG_POPUP_PASTELASTMESSAGE);
		m_nPopupPasteLastMessage = nCmd++;
		mPopup.AppendMenu(MF_STRING | MF_ENABLED, m_nPopupPasteLastMessage, sMenuItemText);

		sMenuItemText.LoadString(IDS_COMMITDLG_POPUP_LOGHISTORY);
		m_nPopupRecentMessage = nCmd++;
		mPopup.AppendMenu(MF_STRING | MF_ENABLED, m_nPopupRecentMessage, sMenuItemText);
	}
}

bool CCommitDlg::HandleMenuItemClick(int cmd, CSciEdit * pSciEdit)
{
	if (cmd == m_nPopupPickCommitHash)
	{
		// use the git log to allow selection of a version
		CLogDlg dlg;
		// tell the dialog to use mode for selecting revisions
		dlg.SetSelect(true);
		// only one revision must be selected however
		dlg.SingleSelection(true);
		if (dlg.DoModal() == IDOK)
		{
			// get selected hash if any
			CString selectedHash = dlg.GetSelectedHash();
			pSciEdit->InsertText(selectedHash);
		}
		return true;
	}

	if (cmd == m_nPopupPickCommitMessage)
	{
		// use the git log to allow selection of a version
		CLogDlg dlg;
		// tell the dialog to use mode for selecting revisions
		dlg.SetSelect(true);
		// only one revision must be selected however
		dlg.SingleSelection(true);
		if (dlg.DoModal() == IDOK)
		{
			// get selected hash if any
			CString selectedHash = dlg.GetSelectedHash();
			GitRev rev;
			rev.GetCommit(selectedHash);
			CString message = rev.GetSubject() + _T("\r\n") + rev.GetBody();
			pSciEdit->InsertText(message);
		}
		return true;
	}

	if (cmd == m_nPopupPasteListCmd)
	{
		CString logmsg;
		int nListItems = m_ListCtrl.GetItemCount();
		for (int i=0; i<nListItems; ++i)
		{
			CTGitPath * entry = (CTGitPath*)m_ListCtrl.GetItemData(i);
			if (entry&&entry->m_Checked)
			{
				CString line;
				CString status = entry->GetActionName();
				if(entry->m_Action & CTGitPath::LOGACTIONS_UNVER)
					status = _T("Add"); // I18N TODO

				//git_wc_status_kind status = entry->status;
				WORD langID = (WORD)CRegStdDWORD(_T("Software\\TortoiseGit\\LanguageID"), GetUserDefaultLangID());
				if (m_ProjectProperties.bFileListInEnglish)
					langID = 1033;

				line.Format(_T("%-10s %s\r\n"),status , (LPCTSTR)m_ListCtrl.GetItemText(i,0));
				logmsg += line;
			}
		}
		pSciEdit->InsertText(logmsg);
		return true;
	}

	if(cmd == m_nPopupPasteLastMessage)
	{
		if (m_History.IsEmpty())
			return false;

		CString logmsg;
		logmsg +=m_History.GetEntry(0);
		pSciEdit->InsertText(logmsg);
		return true;
	}

	if(cmd == m_nPopupRecentMessage )
	{
		OnBnClickedHistory();
		return true;
	}
	return false;
}

void CCommitDlg::OnTimer(UINT_PTR nIDEvent)
{
	switch (nIDEvent)
	{
	case ENDDIALOGTIMER:
		KillTimer(ENDDIALOGTIMER);
		EndDialog(0);
		break;
	case REFRESHTIMER:
		if (m_bThreadRunning)
		{
			SetTimer(REFRESHTIMER, 200, NULL);
			CTraceToOutputDebugString::Instance()(__FUNCTION__ ": Wait some more before refreshing\n");
		}
		else
		{
			KillTimer(REFRESHTIMER);
			CTraceToOutputDebugString::Instance()(__FUNCTION__ ": Refreshing after items dropped\n");
			Refresh();
		}
		break;
	case FILLPATCHVTIMER:
		FillPatchView();
		break;
	}
	__super::OnTimer(nIDEvent);
}

void CCommitDlg::OnBnClickedHistory()
{
	m_tooltips.Pop();	// hide the tooltips
	if (m_pathList.IsEmpty())
		return;

	CHistoryDlg historyDlg;
	historyDlg.SetHistory(m_History);
	if (historyDlg.DoModal() != IDOK)
		return;

	CString sMsg = historyDlg.GetSelectedText();
	if (sMsg != m_cLogMessage.GetText().Left(sMsg.GetLength()))
	{
		CString sBugID = m_ProjectProperties.FindBugID(sMsg);
		if ((!sBugID.IsEmpty()) && ((GetDlgItem(IDC_BUGID)->IsWindowVisible())))
		{
			SetDlgItemText(IDC_BUGID, sBugID);
		}
		if (m_sLogTemplate.Compare(m_cLogMessage.GetText()) != 0)
			m_cLogMessage.InsertText(sMsg, !m_cLogMessage.GetText().IsEmpty());
		else
			m_cLogMessage.SetText(sMsg);
	}

	SendMessage(WM_UPDATEOKBUTTON);
	GetDlgItem(IDC_LOGMESSAGE)->SetFocus();

}

void CCommitDlg::OnBnClickedBugtraqbutton()
{
	m_tooltips.Pop();	// hide the tooltips
	CString sMsg = m_cLogMessage.GetText();

	if (m_BugTraqProvider == NULL)
		return;

	BSTR parameters = m_bugtraq_association.GetParameters().AllocSysString();
	BSTR commonRoot = SysAllocString(g_Git.m_CurrentDir);
	SAFEARRAY *pathList = SafeArrayCreateVector(VT_BSTR, 0, m_pathList.GetCount());

	for (LONG index = 0; index < m_pathList.GetCount(); ++index)
		SafeArrayPutElement(pathList, &index, m_pathList[index].GetGitPathString().AllocSysString());

	BSTR originalMessage = sMsg.AllocSysString();
	BSTR temp = NULL;
//	m_revProps.clear();

	// first try the IBugTraqProvider2 interface
	CComPtr<IBugTraqProvider2> pProvider2 = NULL;
	HRESULT hr = m_BugTraqProvider.QueryInterface(&pProvider2);
	bool bugIdOutSet = false;
	if (SUCCEEDED(hr))
	{
		//CString common = m_ListCtrl.GetCommonURL(false).GetGitPathString();
		BSTR repositoryRoot = g_Git.m_CurrentDir.AllocSysString();
		BSTR bugIDOut = NULL;
		GetDlgItemText(IDC_BUGID, m_sBugID);
		BSTR bugID = m_sBugID.AllocSysString();
		SAFEARRAY * revPropNames = NULL;
		SAFEARRAY * revPropValues = NULL;
		if (FAILED(hr = pProvider2->GetCommitMessage2(GetSafeHwnd(), parameters, repositoryRoot, commonRoot, pathList, originalMessage, bugID, &bugIDOut, &revPropNames, &revPropValues, &temp)))
		{
			CString sErr;
			sErr.Format(IDS_ERR_FAILEDISSUETRACKERCOM, m_bugtraq_association.GetProviderName(), _com_error(hr).ErrorMessage());
			CMessageBox::Show(m_hWnd, sErr, _T("TortoiseGit"), MB_ICONERROR);
		}
		else
		{
			if (bugIDOut)
			{
				bugIdOutSet = true;
				m_sBugID = bugIDOut;
				SysFreeString(bugIDOut);
				SetDlgItemText(IDC_BUGID, m_sBugID);
			}
			SysFreeString(bugID);
			SysFreeString(repositoryRoot);
			m_cLogMessage.SetText(temp);
			BSTR HUGEP *pbRevNames;
			BSTR HUGEP *pbRevValues;

			HRESULT hr1 = SafeArrayAccessData(revPropNames, (void HUGEP**)&pbRevNames);
			if (SUCCEEDED(hr1))
			{
				HRESULT hr2 = SafeArrayAccessData(revPropValues, (void HUGEP**)&pbRevValues);
				if (SUCCEEDED(hr2))
				{
					if (revPropNames->rgsabound->cElements == revPropValues->rgsabound->cElements)
					{
						for (ULONG i = 0; i < revPropNames->rgsabound->cElements; ++i)
						{
//							m_revProps[pbRevNames[i]] = pbRevValues[i];
						}
					}
					SafeArrayUnaccessData(revPropValues);
				}
				SafeArrayUnaccessData(revPropNames);
			}
			if (revPropNames)
				SafeArrayDestroy(revPropNames);
			if (revPropValues)
				SafeArrayDestroy(revPropValues);
		}
	}
	else
	{
		// if IBugTraqProvider2 failed, try IBugTraqProvider
		CComPtr<IBugTraqProvider> pProvider = NULL;
		hr = m_BugTraqProvider.QueryInterface(&pProvider);
		if (FAILED(hr))
		{
			CString sErr;
			sErr.Format(IDS_ERR_FAILEDISSUETRACKERCOM, (LPCTSTR)m_bugtraq_association.GetProviderName(), _com_error(hr).ErrorMessage());
			CMessageBox::Show(m_hWnd, sErr, _T("TortoiseGit"), MB_ICONERROR);
			SysFreeString(parameters);
			SysFreeString(commonRoot);
			SafeArrayDestroy(pathList);
			SysFreeString(originalMessage);
			return;
		}

		if (FAILED(hr = pProvider->GetCommitMessage(GetSafeHwnd(), parameters, commonRoot, pathList, originalMessage, &temp)))
		{
			CString sErr;
			sErr.Format(IDS_ERR_FAILEDISSUETRACKERCOM, m_bugtraq_association.GetProviderName(), _com_error(hr).ErrorMessage());
			CMessageBox::Show(m_hWnd, sErr, _T("TortoiseGit"), MB_ICONERROR);
		}
		else
			m_cLogMessage.SetText(temp);
	}
	m_sLogMessage = m_cLogMessage.GetText();
	if (!m_ProjectProperties.sMessage.IsEmpty())
	{
		CString sBugID = m_ProjectProperties.FindBugID(m_sLogMessage);
		if (!sBugID.IsEmpty() && !bugIdOutSet)
		{
			SetDlgItemText(IDC_BUGID, sBugID);
		}
	}

	m_cLogMessage.SetFocus();

	SysFreeString(parameters);
	SysFreeString(commonRoot);
	SafeArrayDestroy(pathList);
	SysFreeString(originalMessage);
	SysFreeString(temp);
}

void CCommitDlg::FillPatchView(bool onlySetTimer)
{
	if(::IsWindow(this->m_patchViewdlg.m_hWnd))
	{
		KillTimer(FILLPATCHVTIMER);
		if (onlySetTimer)
		{
			SetTimer(FILLPATCHVTIMER, 100, NULL);
			return;
		}
		m_patchViewdlg.m_ctrlPatchView.SetText(CString());

		POSITION pos=m_ListCtrl.GetFirstSelectedItemPosition();
		m_patchViewdlg.m_ctrlPatchView.Call(SCI_SETREADONLY, FALSE);
		CString cmd,out;

		while(pos)
		{
			int nSelect = m_ListCtrl.GetNextSelectedItem(pos);
			CTGitPath * p=(CTGitPath*)m_ListCtrl.GetItemData(nSelect);
			if(p && !(p->m_Action&CTGitPath::LOGACTIONS_UNVER) )
			{
				CString head = _T("HEAD");
				if(m_bCommitAmend==TRUE && m_bAmendDiffToLastCommit==FALSE)
					head = _T("HEAD~1");
				cmd.Format(_T("git.exe diff %s -- \"%s\""), head, p->GetGitPathString());
				g_Git.Run(cmd, &out, CP_UTF8);
			}
		}

		m_patchViewdlg.m_ctrlPatchView.SetText(out);
		m_patchViewdlg.m_ctrlPatchView.Call(SCI_SETREADONLY, TRUE);
		m_patchViewdlg.m_ctrlPatchView.Call(SCI_GOTOPOS, 0);
		CRect rect;
		m_patchViewdlg.m_ctrlPatchView.GetClientRect(rect);
		m_patchViewdlg.m_ctrlPatchView.Call(SCI_SETSCROLLWIDTH, rect.Width() - 4);
	}
}
LRESULT CCommitDlg::OnGitStatusListCtrlItemChanged(WPARAM /*wparam*/, LPARAM /*lparam*/)
{
	this->FillPatchView(true);
	return 0;
}


LRESULT CCommitDlg::OnGitStatusListCtrlCheckChanged(WPARAM, LPARAM)
{
	SendMessage(WM_UPDATEOKBUTTON);
	return 0;
}

LRESULT CCommitDlg::OnCheck(WPARAM wnd, LPARAM)
{
	HWND hwnd = (HWND)wnd;
	bool check = !(GetAsyncKeyState(VK_SHIFT) & 0x8000);
	if (hwnd == GetDlgItem(IDC_CHECKALL)->GetSafeHwnd())
		m_ListCtrl.Check(GITSLC_SHOWEVERYTHING, check);
	else if (hwnd == GetDlgItem(IDC_CHECKNONE)->GetSafeHwnd())
		m_ListCtrl.Check(GITSLC_SHOWEVERYTHING, !check);
	else if (hwnd == GetDlgItem(IDC_CHECKUNVERSIONED)->GetSafeHwnd())
		m_ListCtrl.Check(GITSLC_SHOWUNVERSIONED, check);
	else if (hwnd == GetDlgItem(IDC_CHECKVERSIONED)->GetSafeHwnd())
		m_ListCtrl.Check(GITSLC_SHOWVERSIONED, check);
	else if (hwnd == GetDlgItem(IDC_CHECKADDED)->GetSafeHwnd())
		m_ListCtrl.Check(GITSLC_SHOWADDED, check);
	else if (hwnd == GetDlgItem(IDC_CHECKDELETED)->GetSafeHwnd())
		m_ListCtrl.Check(GITSLC_SHOWREMOVED, check);
	else if (hwnd == GetDlgItem(IDC_CHECKMODIFIED)->GetSafeHwnd())
		m_ListCtrl.Check(GITSLC_SHOWMODIFIED, check);
	else if (hwnd == GetDlgItem(IDC_CHECKFILES)->GetSafeHwnd())
		m_ListCtrl.Check(GITSLC_SHOWFILES, check);
	else if (hwnd == GetDlgItem(IDC_CHECKSUBMODULES)->GetSafeHwnd())
		m_ListCtrl.Check(GITSLC_SHOWSUBMODULES, check);

	return 0;
}

LRESULT CCommitDlg::OnUpdateOKButton(WPARAM, LPARAM)
{
	if (m_bBlock)
		return 0;

	bool bValidLogSize = m_cLogMessage.GetText().GetLength() >= m_ProjectProperties.nMinLogSize && m_cLogMessage.GetText().GetLength() > 0;
	bool bAmendOrSelectFilesOrMerge = m_ListCtrl.GetSelected() > 0 || (m_bCommitAmend && m_bAmendDiffToLastCommit) || CTGitPath(g_Git.m_CurrentDir).IsMergeActive();

	DialogEnableWindow(IDOK, bValidLogSize && (m_bCommitMessageOnly || bAmendOrSelectFilesOrMerge));

	return 0;
}

LRESULT CCommitDlg::OnUpdateDataFalse(WPARAM, LPARAM)
{
	UpdateData(FALSE);
	return 0;
}

LRESULT CCommitDlg::DefWindowProc(UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message) {
	case WM_NOTIFY:
		if (wParam == IDC_SPLITTER)
		{
			SPC_NMHDR* pHdr = (SPC_NMHDR*) lParam;
			DoSize(pHdr->delta);
		}
		break;
	}

	return __super::DefWindowProc(message, wParam, lParam);
}

void CCommitDlg::SetSplitterRange()
{
	if ((m_ListCtrl)&&(m_cLogMessage))
	{
		CRect rcTop;
		m_cLogMessage.GetWindowRect(rcTop);
		ScreenToClient(rcTop);
		CRect rcMiddle;
		m_ListCtrl.GetWindowRect(rcMiddle);
		ScreenToClient(rcMiddle);
		if (rcMiddle.Height() && rcMiddle.Width())
			m_wndSplitter.SetRange(rcTop.top + 100, rcMiddle.bottom - 80);
	}
}

void CCommitDlg::DoSize(int delta)
{
	RemoveAnchor(IDC_MESSAGEGROUP);
	RemoveAnchor(IDC_LOGMESSAGE);
	RemoveAnchor(IDC_SPLITTER);
	RemoveAnchor(IDC_SIGNOFF);
	RemoveAnchor(IDC_COMMIT_AMEND);
	RemoveAnchor(IDC_COMMIT_AMENDDIFF);
	RemoveAnchor(IDC_COMMIT_SETDATETIME);
	RemoveAnchor(IDC_COMMIT_DATEPICKER);
	RemoveAnchor(IDC_COMMIT_TIMEPICKER);
	RemoveAnchor(IDC_COMMIT_SETAUTHOR);
	RemoveAnchor(IDC_COMMIT_AUTHORDATA);
	RemoveAnchor(IDC_LISTGROUP);
	RemoveAnchor(IDC_FILELIST);
	RemoveAnchor(IDC_TEXT_INFO);
	RemoveAnchor(IDC_SELECTLABEL);
	RemoveAnchor(IDC_CHECKALL);
	RemoveAnchor(IDC_CHECKNONE);
	RemoveAnchor(IDC_CHECKUNVERSIONED);
	RemoveAnchor(IDC_CHECKVERSIONED);
	RemoveAnchor(IDC_CHECKADDED);
	RemoveAnchor(IDC_CHECKDELETED);
	RemoveAnchor(IDC_CHECKMODIFIED);
	RemoveAnchor(IDC_CHECKFILES);
	RemoveAnchor(IDC_CHECKSUBMODULES);

	CSplitterControl::ChangeHeight(&m_cLogMessage, delta, CW_TOPALIGN);
	CSplitterControl::ChangeHeight(GetDlgItem(IDC_MESSAGEGROUP), delta, CW_TOPALIGN);
	CSplitterControl::ChangeHeight(&m_ListCtrl, -delta, CW_BOTTOMALIGN);
	CSplitterControl::ChangeHeight(GetDlgItem(IDC_LISTGROUP), -delta, CW_BOTTOMALIGN);
	CSplitterControl::ChangePos(GetDlgItem(IDC_SIGNOFF),0,delta);
	CSplitterControl::ChangePos(GetDlgItem(IDC_COMMIT_AMEND),0,delta);
	CSplitterControl::ChangePos(GetDlgItem(IDC_COMMIT_AMENDDIFF),0,delta);
	CSplitterControl::ChangePos(GetDlgItem(IDC_COMMIT_SETDATETIME),0,delta);
	CSplitterControl::ChangePos(GetDlgItem(IDC_COMMIT_DATEPICKER),0,delta);
	CSplitterControl::ChangePos(GetDlgItem(IDC_COMMIT_TIMEPICKER),0,delta);
	CSplitterControl::ChangePos(GetDlgItem(IDC_COMMIT_SETAUTHOR), 0, delta);
	CSplitterControl::ChangePos(GetDlgItem(IDC_COMMIT_AUTHORDATA), 0, delta);
	CSplitterControl::ChangePos(GetDlgItem(IDC_TEXT_INFO),0,delta);
	CSplitterControl::ChangePos(GetDlgItem(IDC_SELECTLABEL), 0, delta);
	CSplitterControl::ChangePos(GetDlgItem(IDC_CHECKALL), 0, delta);
	CSplitterControl::ChangePos(GetDlgItem(IDC_CHECKNONE), 0, delta);
	CSplitterControl::ChangePos(GetDlgItem(IDC_CHECKUNVERSIONED), 0, delta);
	CSplitterControl::ChangePos(GetDlgItem(IDC_CHECKVERSIONED), 0, delta);
	CSplitterControl::ChangePos(GetDlgItem(IDC_CHECKADDED), 0, delta);
	CSplitterControl::ChangePos(GetDlgItem(IDC_CHECKDELETED), 0, delta);
	CSplitterControl::ChangePos(GetDlgItem(IDC_CHECKMODIFIED), 0, delta);
	CSplitterControl::ChangePos(GetDlgItem(IDC_CHECKFILES), 0, delta);
	CSplitterControl::ChangePos(GetDlgItem(IDC_CHECKSUBMODULES), 0, delta);

	AddAnchor(IDC_MESSAGEGROUP, TOP_LEFT, TOP_RIGHT);
	AddAnchor(IDC_LOGMESSAGE, TOP_LEFT, TOP_RIGHT);
	AddAnchor(IDC_SPLITTER, TOP_LEFT, TOP_RIGHT);
	AddAnchor(IDC_LISTGROUP, TOP_LEFT, BOTTOM_RIGHT);
	AddAnchor(IDC_FILELIST, TOP_LEFT, BOTTOM_RIGHT);
	AddAnchor(IDC_SIGNOFF,TOP_RIGHT);
	AddAnchor(IDC_COMMIT_AMEND,TOP_LEFT);
	AddAnchor(IDC_COMMIT_AMENDDIFF,TOP_LEFT);
	AddAnchor(IDC_COMMIT_SETDATETIME,TOP_LEFT);
	AddAnchor(IDC_COMMIT_DATEPICKER,TOP_LEFT);
	AddAnchor(IDC_COMMIT_TIMEPICKER,TOP_LEFT);
	AddAnchor(IDC_COMMIT_SETAUTHOR, TOP_LEFT);
	AddAnchor(IDC_COMMIT_AUTHORDATA, TOP_LEFT, TOP_RIGHT);
	AddAnchor(IDC_TEXT_INFO,TOP_RIGHT);
	AddAnchor(IDC_SELECTLABEL, TOP_LEFT);
	AddAnchor(IDC_CHECKALL, TOP_LEFT);
	AddAnchor(IDC_CHECKNONE, TOP_LEFT);
	AddAnchor(IDC_CHECKUNVERSIONED, TOP_LEFT);
	AddAnchor(IDC_CHECKVERSIONED, TOP_LEFT);
	AddAnchor(IDC_CHECKADDED, TOP_LEFT);
	AddAnchor(IDC_CHECKDELETED, TOP_LEFT);
	AddAnchor(IDC_CHECKMODIFIED, TOP_LEFT);
	AddAnchor(IDC_CHECKFILES, TOP_LEFT);
	AddAnchor(IDC_CHECKSUBMODULES, TOP_LEFT);
	ArrangeLayout();
	// adjust the minimum size of the dialog to prevent the resizing from
	// moving the list control too far down.
	CRect rcLogMsg;
	m_cLogMessage.GetClientRect(rcLogMsg);
	SetMinTrackSize(CSize(m_DlgOrigRect.Width(), m_DlgOrigRect.Height()-m_LogMsgOrigRect.Height()+rcLogMsg.Height()));

	SetSplitterRange();
	m_cLogMessage.Invalidate();
	GetDlgItem(IDC_LOGMESSAGE)->Invalidate();
}

void CCommitDlg::OnSize(UINT nType, int cx, int cy)
{
	// first, let the resizing take place
	__super::OnSize(nType, cx, cy);

	//set range
	SetSplitterRange();
}

CString CCommitDlg::GetSignedOffByLine()
{
	CString str;

	CString username = g_Git.GetUserName();
	CString email = g_Git.GetUserEmail();
	username.Remove(_T('\n'));
	email.Remove(_T('\n'));

	str.Format(_T("Signed-off-by: %s <%s>"), username, email);

	return str;
}

void CCommitDlg::OnBnClickedSignOff()
{
	CString str = GetSignedOffByLine();

	if (m_cLogMessage.GetText().Find(str) == -1) {
		m_cLogMessage.SetText(m_cLogMessage.GetText().TrimRight());
		int lastNewline = m_cLogMessage.GetText().ReverseFind(_T('\n'));
		int foundByLine = -1;
		if (lastNewline > 0)
			foundByLine = m_cLogMessage.GetText().Find(_T("-by: "), lastNewline);

		if (foundByLine == -1 || foundByLine < lastNewline)
			str = _T("\r\n") + str;

		m_cLogMessage.SetText(m_cLogMessage.GetText()+_T("\r\n")+str+_T("\r\n"));
	}
}

void CCommitDlg::OnBnClickedCommitAmend()
{
	this->UpdateData();
	if(this->m_bCommitAmend && this->m_AmendStr.IsEmpty())
	{
		GitRev rev;
		try
		{
			rev.GetCommit(CString(_T("HEAD")));
		}
		catch (const char *msg)
		{
			CMessageBox::Show(m_hWnd, _T("Could not get HEAD commit.\nlibgit reports:\n") + CString(msg), _T("TortoiseGit"), MB_ICONERROR);
		}
		m_AmendStr=rev.GetSubject()+_T("\n")+rev.GetBody();
	}

	if(this->m_bCommitAmend)
	{
		this->m_NoAmendStr=this->m_cLogMessage.GetText();
		m_cLogMessage.SetText(m_AmendStr);
		GetDlgItem(IDC_COMMIT_AMENDDIFF)->ShowWindow(SW_SHOW);
	}
	else
	{
		this->m_AmendStr=this->m_cLogMessage.GetText();
		m_cLogMessage.SetText(m_NoAmendStr);
		GetDlgItem(IDC_COMMIT_AMENDDIFF)->ShowWindow(SW_HIDE);
	}

	OnBnClickedCommitSetDateTime(); // to update the commit date and time
	OnBnClickedCommitSetauthor(); // to update the commit author

	GetDlgItem(IDC_LOGMESSAGE)->SetFocus();
	Refresh();
}

void CCommitDlg::OnBnClickedCommitMessageOnly()
{
	this->UpdateData();
	this->m_ListCtrl.EnableWindow(m_bCommitMessageOnly ? FALSE : TRUE);
	SendMessage(WM_UPDATEOKBUTTON);
}

void CCommitDlg::OnBnClickedWholeProject()
{
	m_tooltips.Pop();	// hide the tooltips
	UpdateData();
	m_ListCtrl.Clear();
	if (!m_bBlock)
	{
		if(m_bWholeProject)
			m_ListCtrl.GetStatus(NULL,true,false,true);
		else
			m_ListCtrl.GetStatus(&this->m_pathList,true,false,true);

		m_regShowWholeProject = m_bWholeProject;

		DWORD dwShow = m_ListCtrl.GetShowFlags();
		if (DWORD(m_regAddBeforeCommit))
			dwShow |= GITSLC_SHOWUNVERSIONED;
		else
			dwShow &= ~GITSLC_SHOWUNVERSIONED;

		m_ListCtrl.Show(dwShow, dwShow & ~(CTGitPath::LOGACTIONS_UNVER), true);
		UpdateCheckLinks();
	}

	SetDlgTitle();
}

void CCommitDlg::OnFocusMessage()
{
	m_cLogMessage.SetFocus();
}

void CCommitDlg::OnFocusFileList()
{
	m_ListCtrl.SetFocus();
}

void CCommitDlg::OnScnUpdateUI(NMHDR * /*pNMHDR*/, LRESULT *pResult)
{
	int pos = (int)this->m_cLogMessage.Call(SCI_GETCURRENTPOS);
	int line = (int)this->m_cLogMessage.Call(SCI_LINEFROMPOSITION,pos);
	int column = (int)this->m_cLogMessage.Call(SCI_GETCOLUMN,pos);

	CString str;
	str.Format(_T("%d/%d"),line+1,column+1);
	this->GetDlgItem(IDC_TEXT_INFO)->SetWindowText(str);

	if(*pResult)
		*pResult=0;
}
void CCommitDlg::OnStnClickedViewPatch()
{
	m_patchViewdlg.m_pProjectProperties = &this->m_ProjectProperties;
	m_patchViewdlg.m_ParentCommitDlg = this;
	if(!IsWindow(this->m_patchViewdlg.m_hWnd))
	{
		BOOL viewPatchEnabled = FALSE;
		viewPatchEnabled = g_Git.GetConfigValueBool(_T("tgit.commitshowpatch"));
		if (viewPatchEnabled == FALSE)
			g_Git.SetConfigValue(_T("tgit.commitshowpatch"), _T("true"));
		m_patchViewdlg.Create(IDD_PATCH_VIEW,this);
		m_patchViewdlg.m_ctrlPatchView.Call(SCI_SETSCROLLWIDTHTRACKING, TRUE);
		CRect rect;
		this->GetWindowRect(&rect);

		m_patchViewdlg.ShowWindow(SW_SHOW);

		m_patchViewdlg.SetWindowPos(NULL,rect.right,rect.top,rect.Width(),rect.Height(),
				SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOZORDER);

		ShowViewPatchText(false);
		FillPatchView();
	}
	else
	{
		g_Git.SetConfigValue(_T("tgit.commitshowpatch"), _T("false"));
		m_patchViewdlg.ShowWindow(SW_HIDE);
		m_patchViewdlg.DestroyWindow();
		ShowViewPatchText(true);
	}
	this->m_ctrlShowPatch.Invalidate();
}

void CCommitDlg::OnMoving(UINT fwSide, LPRECT pRect)
{
	__super::OnMoving(fwSide, pRect);

	if (::IsWindow(m_patchViewdlg.m_hWnd))
	{
		RECT patchrect;
		m_patchViewdlg.GetWindowRect(&patchrect);
		if (::IsWindow(m_hWnd))
		{
			RECT thisrect;
			GetWindowRect(&thisrect);
			if (patchrect.left == thisrect.right)
			{
				m_patchViewdlg.SetWindowPos(NULL, patchrect.left - (thisrect.left - pRect->left), patchrect.top - (thisrect.top - pRect->top),
					0, 0, SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOSIZE | SWP_NOZORDER);
			}
		}
	}

}

void CCommitDlg::OnSizing(UINT fwSide, LPRECT pRect)
{
	__super::OnSizing(fwSide, pRect);

	if(::IsWindow(this->m_patchViewdlg.m_hWnd))
	{
		CRect thisrect, patchrect;
		this->GetWindowRect(thisrect);
		this->m_patchViewdlg.GetWindowRect(patchrect);
		if(thisrect.right==patchrect.left)
		{
			patchrect.left -= (thisrect.right - pRect->right);
			patchrect.right-= (thisrect.right - pRect->right);

			if(	patchrect.bottom == thisrect.bottom)
			{
				patchrect.bottom -= (thisrect.bottom - pRect->bottom);
			}
			if(	patchrect.top == thisrect.top)
			{
				patchrect.top -=  thisrect.top-pRect->top;
			}
			m_patchViewdlg.MoveWindow(patchrect);
		}
	}
}

void CCommitDlg::OnHdnItemchangedFilelist(NMHDR * /*pNMHDR*/, LRESULT *pResult)
{
	*pResult = 0;
	TRACE("Item Changed\r\n");
}

int CCommitDlg::CheckHeadDetach()
{
	CString output;
	if (CGit::GetCurrentBranchFromFile(g_Git.m_CurrentDir, output))
	{
		int retval = CMessageBox::Show(NULL, IDS_PROC_COMMIT_DETACHEDWARNING, IDS_APPNAME, MB_YESNOCANCEL | MB_ICONWARNING);
		if(retval == IDYES)
		{
			if (CAppUtils::CreateBranchTag(FALSE, NULL, true) == FALSE)
				return 1;
		}
		else if (retval == IDCANCEL)
			return 1;
	}
	return 0;
}

void CCommitDlg::OnBnClickedCommitAmenddiff()
{
	UpdateData();
	Refresh();
}

void CCommitDlg::OnBnClickedNoautoselectsubmodules()
{
	UpdateData();
	Refresh();
}

void CCommitDlg::OnBnClickedCommitSetDateTime()
{
	UpdateData();

	if (m_bSetCommitDateTime)
	{
		CTime authordate = CTime::GetCurrentTime();
		if (m_bCommitAmend)
		{
			GitRev headRevision;
			try
			{
				headRevision.GetCommit(_T("HEAD"));
			}
			catch (const char *msg)
			{
				CMessageBox::Show(m_hWnd, _T("Could not get HEAD commit.\nlibgit reports:\n") + CString(msg), _T("TortoiseGit"), MB_ICONERROR);
			}
			authordate = headRevision.GetAuthorDate();
		}

		m_CommitDate.SetTime(&authordate);
		m_CommitTime.SetTime(&authordate);

		GetDlgItem(IDC_COMMIT_DATEPICKER)->ShowWindow(SW_SHOW);
		GetDlgItem(IDC_COMMIT_TIMEPICKER)->ShowWindow(SW_SHOW);
	}
	else
	{
		GetDlgItem(IDC_COMMIT_DATEPICKER)->ShowWindow(SW_HIDE);
		GetDlgItem(IDC_COMMIT_TIMEPICKER)->ShowWindow(SW_HIDE);
	}
}

void CCommitDlg::OnBnClickedCheckNewBranch()
{
	UpdateData();
	if (m_bCreateNewBranch)
	{
		GetDlgItem(IDC_COMMIT_TO)->ShowWindow(SW_HIDE);
		GetDlgItem(IDC_NEWBRANCH)->ShowWindow(SW_SHOW);
	}
	else
	{
		GetDlgItem(IDC_NEWBRANCH)->ShowWindow(SW_HIDE);
		GetDlgItem(IDC_COMMIT_TO)->ShowWindow(SW_SHOW);
	}
}

void CCommitDlg::RestoreFiles(bool doNotAsk)
{
	if (!m_ListCtrl.m_restorepaths.empty() && (doNotAsk || CMessageBox::Show(m_hWnd, IDS_PROC_COMMIT_RESTOREFILES, IDS_APPNAME, 2, IDI_QUESTION, IDS_PROC_COMMIT_RESTOREFILES_RESTORE, IDS_PROC_COMMIT_RESTOREFILES_KEEP) == 1))
	{
		for (std::map<CString, CString>::iterator it = m_ListCtrl.m_restorepaths.begin(); it != m_ListCtrl.m_restorepaths.end(); ++it)
			CopyFile(it->second, g_Git.m_CurrentDir + _T("\\") + it->first, FALSE);
		m_ListCtrl.m_restorepaths.clear();
	}
}

void CCommitDlg::UpdateCheckLinks()
{
	DialogEnableWindow(IDC_CHECKALL, true);
	DialogEnableWindow(IDC_CHECKNONE, true);
	DialogEnableWindow(IDC_CHECKUNVERSIONED, m_ListCtrl.GetUnversionedCount() > 0);
	DialogEnableWindow(IDC_CHECKVERSIONED, m_ListCtrl.GetItemCount() > m_ListCtrl.GetUnversionedCount());
	DialogEnableWindow(IDC_CHECKADDED, m_ListCtrl.GetAddedCount() > 0);
	DialogEnableWindow(IDC_CHECKDELETED, m_ListCtrl.GetDeletedCount() > 0);
	DialogEnableWindow(IDC_CHECKMODIFIED, m_ListCtrl.GetModifiedCount() > 0);
	DialogEnableWindow(IDC_CHECKFILES, m_ListCtrl.GetFileCount() > 0);
	DialogEnableWindow(IDC_CHECKSUBMODULES, m_ListCtrl.GetSubmoduleCount() > 0);
}

void CCommitDlg::OnBnClickedCommitSetauthor()
{
	UpdateData();

	if (m_bSetAuthor)
	{
		m_sAuthor.Format(_T("%s <%s>"), g_Git.GetUserName(), g_Git.GetUserEmail());
		if (m_bCommitAmend)
		{
			GitRev headRevision;
			try
			{
				headRevision.GetCommit(_T("HEAD"));
			}
			catch (const char *msg)
			{
				CMessageBox::Show(m_hWnd, _T("Could not get HEAD commit.\nlibgit reports:\n") + CString(msg), _T("TortoiseGit"), MB_ICONERROR);
			}
			m_sAuthor.Format(_T("%s <%s>"), headRevision.GetAuthorName(), headRevision.GetAuthorEmail());
		}

		UpdateData(FALSE);

		GetDlgItem(IDC_COMMIT_AUTHORDATA)->ShowWindow(SW_SHOW);
	}
	else
		GetDlgItem(IDC_COMMIT_AUTHORDATA)->ShowWindow(SW_HIDE);
}
