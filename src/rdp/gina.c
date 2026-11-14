/* ntrdps - RDP console server for Windows NT
 * Copyright (c) 2026 ntrdps authors. MIT License, see LICENSE. */

#include <windows.h>
#include <string.h>
#include "log.h"
#include "gina.h"

#define X_WLX_VERSION_1_3               0x00010003L
#define X_WLX_VERSION_1_4               0x00010004L
#define X_WLX_SAS_TYPE_CTRL_ALT_DEL     1
#define X_WLX_SAS_ACTION_LOGON          1
#define X_WLX_DLG_INPUT_TIMEOUT         102
#define X_WLX_OPTION_USE_CTRL_ALT_DEL   0x00000001L
#define X_WLX_OPTION_CONTEXT_POINTER    0x00000002L
#define X_WLX_OPTION_USE_SMART_CARD     0x00000003L
#define X_WLX_OPTION_FORCE_LOGOFF_TIME  0x00000004L
#define X_WLX_OPTION_IGNORE_AUTO_LOGON  0x00000008L
#define X_WLX_OPTION_SMART_CARD_PRESENT 0x00010001L
#define X_WLX_OPTION_SMART_CARD_INFO    0x00010002L
#define X_WLX_OPTION_TABLE_SIZE         0x00010003L
#define X_WLX_DESKTOP_NAME              0x00000001L
#define X_WLX_DESKTOP_HANDLE            0x00000002L
#define X_WLX_PROFILE_TYPE_V2_0         2

#define X_STATUS_ACCESS_DENIED          0xC0000022L
#define X_STATUS_NOT_FOUND              0xC0000034L

#define X_EM_SETMARGINS                 0x00D3
#define X_EC_BOTH                       0x0003
#define X_EC_USEFONTINFO                0xFFFF

#define PROP_NAME   "ntrdps-gina"
#define ID_USER     1502
#define ID_PW       1503
#define N_1_3       25
#define N_1_4       27
#define EXTRA       8

#ifdef _WIN64
typedef unsigned __int64 X_UPTR;
typedef INT_PTR          X_IPTR;
#else
typedef unsigned long    X_UPTR;
typedef BOOL             X_IPTR;
#endif

typedef X_IPTR (CALLBACK *X_DLGPROC)(HWND, UINT, WPARAM, LPARAM);
typedef BOOL (WINAPI *G_NEGOTIATE)(DWORD, DWORD *);
typedef BOOL (WINAPI *G_INITIALIZE)(LPWSTR, HANDLE, PVOID, PVOID, PVOID *);
typedef int  (WINAPI *G_LOGGED_OUT)(PVOID, DWORD, LUID *, PSID, DWORD *,
                                    HANDLE *, PVOID, PVOID *);

typedef struct {
    FARPROC use_ctrl_alt_del, set_context_pointer, sas_notify, set_timeout,
            assign_shell_protection, message_box, dialog_box,
            dialog_box_param, dialog_box_indirect, dialog_box_indirect_param,
            switch_to_user, switch_to_winlogon, change_password_notify,
            get_source_desktop, set_return_desktop, create_user_desktop,
            change_password_notify_ex, close_user_desktop, set_option,
            get_option, win31_migrate, query_client_credentials,
            query_ic_credentials, disconnect, query_ts_data;
    FARPROC extra[EXTRA];
} X_DISPATCH;

typedef struct {
    DWORD  size, flags;
    HANDLE desk;
    PWSTR  name;
} X_DESKTOP;

typedef struct {
    PWSTR user, domain, pw, old_pw;
} X_MPR;

typedef struct {
    DWORD type;
    PWSTR profile, policy, net_default, server, env;
} X_PROFILE;

typedef struct {
    USHORT len, max;
    PWSTR  buf;
} X_USTR;

typedef struct {
    X_DLGPROC proc;
    LPARAM    param;
    int       logon;
} CALL;

static LONG          g_spin;
static int           g_state;
static HMODULE       g_mod;
static char          g_path[MAX_PATH + 16];
static G_LOGGED_OUT  g_logged_out;
static PVOID         g_wlx;
static DWORD         g_ver;
static X_DISPATCH    g_table;
static X_UPTR        g_opt[5];
static LONG          g_busy;
static GINA_HOST    *g_host;
static DWORD         g_tid;
static HANDLE        g_desk;
static volatile int  g_ending;
static DWORD         g_end_ms;
static int           g_shown;
static CALL         *g_pending;
static DWORD         g_said;
static DWORD         g_sid[5];
static WCHAR         g_winsta[] = { 'W', 'i', 'n', 'S', 't', 'a', '0', 0 };

static void lock(void)
{
    while (InterlockedExchange(&g_spin, 1) != 0) {
        Sleep(1);
    }
}

static void unlock(void)
{
    InterlockedExchange(&g_spin, 0);
}

static int on_thread(void)
{
    return g_tid != 0 && GetCurrentThreadId() == g_tid;
}

static int ending(void)
{
    GINA_HOST *h = g_host;

    return g_ending || (h != NULL && on_thread() && h->ending(h->ctx));
}

static void once(int bit, const char *what)
{
    if (g_said & (1UL << bit)) {
        return;
    }
    g_said |= 1UL << bit;
    log_msg(LOG_INFO, "login: msgina.dll: %s", what);
}

static UINT  (WINAPI *r_GetProfileIntW)(LPCWSTR, LPCWSTR, INT);
static DWORD (WINAPI *r_GetProfileStringW)(LPCWSTR, LPCWSTR, LPCWSTR,
                                           LPWSTR, DWORD);
static DWORD (WINAPI *r_GetProfileStringA)(LPCSTR, LPCSTR, LPCSTR, LPSTR,
                                           DWORD);
static DWORD (WINAPI *r_GetPrivateProfileStringW)(LPCWSTR, LPCWSTR,
                                                  LPCWSTR, LPWSTR, DWORD,
                                                  LPCWSTR);
static BOOL  (WINAPI *r_WriteProfileStringW)(LPCWSTR, LPCWSTR, LPCWSTR);
static LONG  (WINAPI *r_RegQueryValueExW)(HKEY, LPCWSTR, LPDWORD, LPDWORD,
                                          LPBYTE, LPDWORD);
static LONG  (WINAPI *r_RegSetValueExW)(HKEY, LPCWSTR, DWORD, DWORD,
                                        CONST BYTE *, DWORD);
static LONG  (WINAPI *r_RegDeleteValueW)(HKEY, LPCWSTR);
static LONG  (WINAPI *r_RegDeleteKeyW)(HKEY, LPCWSTR);
static LONG  (WINAPI *r_RegCreateKeyExW)(HKEY, LPCWSTR, DWORD, LPWSTR, DWORD,
                                         DWORD, LPVOID, PHKEY, LPDWORD);
static LONG  (WINAPI *r_LsaRetrievePrivateData)(HANDLE, X_USTR *, X_USTR **);
static LONG  (WINAPI *r_LsaStorePrivateData)(HANDLE, X_USTR *, X_USTR *);
static BOOL  (WINAPI *r_NotifyBootConfigStatus)(BOOL);
static BOOL  (WINAPI *r_CreateProcessAsUserW)(HANDLE, LPCWSTR, LPWSTR,
                                              LPVOID, LPVOID, BOOL, DWORD,
                                              LPVOID, LPCWSTR, LPVOID,
                                              LPVOID);
static VOID  (WINAPI *r_ExitProcess)(UINT);
static BOOL  (WINAPI *r_TerminateProcess)(HANDLE, UINT);
static LONG  (WINAPI *r_NtShutdownSystem)(int);
static BOOL  (WINAPI *r_RefreshPolicy)(BOOL);
static BOOL  (WINAPI *r_SetThreadDesktop)(HANDLE);
static int   (WINAPI *r_MessageBoxW)(HWND, LPCWSTR, LPCWSTR, UINT);
static VOID  (WINAPI *r_DebugBreak)(void);
static VOID  (WINAPI *r_RtlAssert)(PVOID, PVOID, ULONG, PCHAR);
static X_IPTR (WINAPI *r_DialogBoxParamW)(HINSTANCE, LPCWSTR, HWND,
                                          X_DLGPROC, LPARAM);
static HWND  (WINAPI *r_CreateDialogParamW)(HINSTANCE, LPCWSTR, HWND,
                                            X_DLGPROC, LPARAM);

static int show(HANDLE inst, LPCWSTR tmpl, LPDLGTEMPLATEW t, HWND owner,
                X_DLGPROC proc, LPARAM param);

static int hidden_w(LPCWSTR name)
{
    static const WCHAR auto_[] = { 'A','u','t','o','A','d','m','i','n',
                                   'L','o','g','o','n',0 };
    static const WCHAR force[] = { 'F','o','r','c','e','A','u','t','o',
                                   'L','o','g','o','n',0 };
    static const WCHAR count[] = { 'A','u','t','o','L','o','g','o','n',
                                   'C','o','u','n','t',0 };
    static const WCHAR pw[] = { 'D','e','f','a','u','l','t','P','a','s',
                                's','w','o','r','d',0 };

    if (name == NULL || ((X_UPTR) name >> 16) == 0) {
        return 0;
    }
    if (lstrcmpiW(name, auto_) == 0 || lstrcmpiW(name, force) == 0 ||
        lstrcmpiW(name, count) == 0 || lstrcmpiW(name, pw) == 0) {
        once(0, "it asked for the automatic logon: ntrdps says there is "
             "none");
        return 1;
    }
    return 0;
}

static int hidden_a(LPCSTR name)
{
    if (name == NULL || ((X_UPTR) name >> 16) == 0) {
        return 0;
    }
    if (lstrcmpiA(name, "AutoAdminLogon") == 0 ||
        lstrcmpiA(name, "ForceAutoLogon") == 0 ||
        lstrcmpiA(name, "AutoLogonCount") == 0 ||
        lstrcmpiA(name, "DefaultPassword") == 0) {
        once(0, "it asked for the automatic logon: ntrdps says there is "
             "none");
        return 1;
    }
    return 0;
}

static DWORD default_w(LPCWSTR def, LPWSTR out, DWORD n)
{
    DWORD i = 0;

    if (out == NULL || n == 0) {
        return 0;
    }
    while (def != NULL && def[i] != 0 && i + 1 < n) {
        out[i] = def[i];
        i++;
    }
    out[i] = 0;
    return i;
}

static UINT WINAPI h_GetProfileIntW(LPCWSTR app, LPCWSTR key, INT def)
{
    if (hidden_w(key)) {
        return 0;
    }
    return r_GetProfileIntW(app, key, def);
}

static DWORD WINAPI h_GetProfileStringW(LPCWSTR app, LPCWSTR key,
                                        LPCWSTR def, LPWSTR out, DWORD n)
{
    if (hidden_w(key)) {
        return default_w(def, out, n);
    }
    return r_GetProfileStringW(app, key, def, out, n);
}

static DWORD WINAPI h_GetProfileStringA(LPCSTR app, LPCSTR key, LPCSTR def,
                                        LPSTR out, DWORD n)
{
    DWORD i = 0;

    if (!hidden_a(key)) {
        return r_GetProfileStringA(app, key, def, out, n);
    }
    if (out == NULL || n == 0) {
        return 0;
    }
    while (def != NULL && def[i] != '\0' && i + 1 < n) {
        out[i] = def[i];
        i++;
    }
    out[i] = '\0';
    return i;
}

static DWORD WINAPI h_GetPrivateProfileStringW(LPCWSTR app, LPCWSTR key,
                                               LPCWSTR def, LPWSTR out,
                                               DWORD n, LPCWSTR file)
{
    if (hidden_w(key)) {
        return default_w(def, out, n);
    }
    return r_GetPrivateProfileStringW(app, key, def, out, n, file);
}

static BOOL WINAPI h_WriteProfileStringW(LPCWSTR app, LPCWSTR key,
                                         LPCWSTR val)
{
    once(1, "it wanted to write the registry: skipped");
    return TRUE;
}

static LONG WINAPI h_RegQueryValueExW(HKEY k, LPCWSTR name, LPDWORD res,
                                      LPDWORD type, LPBYTE data, LPDWORD n)
{
    if (hidden_w(name)) {
        return ERROR_FILE_NOT_FOUND;
    }
    return r_RegQueryValueExW(k, name, res, type, data, n);
}

static LONG WINAPI h_RegSetValueExW(HKEY k, LPCWSTR name, DWORD res,
                                    DWORD type, CONST BYTE *data, DWORD n)
{
    once(1, "it wanted to write the registry: skipped");
    return ERROR_SUCCESS;
}

static LONG WINAPI h_RegDeleteValueW(HKEY k, LPCWSTR name)
{
    once(1, "it wanted to write the registry: skipped");
    return ERROR_SUCCESS;
}

static LONG WINAPI h_RegDeleteKeyW(HKEY k, LPCWSTR sub)
{
    once(1, "it wanted to write the registry: skipped");
    return ERROR_SUCCESS;
}

static LONG WINAPI h_RegCreateKeyExW(HKEY k, LPCWSTR sub, DWORD res,
                                     LPWSTR cls, DWORD opt, DWORD sam,
                                     LPVOID sa, PHKEY out, LPDWORD disp)
{
    LONG r = RegOpenKeyExW(k, sub, 0, sam, out);

    if (r == ERROR_SUCCESS && disp != NULL) {
        *disp = REG_OPENED_EXISTING_KEY;
    }
    return r;
}

static int ustr_is(const X_USTR *u, const char *s)
{
    unsigned int i, n;

    if (u == NULL || u->buf == NULL) {
        return 0;
    }
    n = u->len / sizeof(WCHAR);
    if (n != strlen(s)) {
        return 0;
    }
    for (i = 0; i < n; i++) {
        WCHAR c = u->buf[i];

        if (c >= 'a' && c <= 'z') {
            c = (WCHAR) (c - 'a' + 'A');
        }
        if (c != (WCHAR) (s[i] >= 'a' && s[i] <= 'z' ? s[i] - 'a' + 'A'
                                                      : s[i])) {
            return 0;
        }
    }
    return 1;
}

static LONG WINAPI h_LsaRetrievePrivateData(HANDLE pol, X_USTR *key,
                                            X_USTR **data)
{
    if (ustr_is(key, "DefaultPassword")) {
        once(0, "it asked for the automatic logon: ntrdps says there is "
             "none");
        if (data != NULL) {
            *data = NULL;
        }
        return X_STATUS_NOT_FOUND;
    }
    return r_LsaRetrievePrivateData(pol, key, data);
}

static LONG WINAPI h_LsaStorePrivateData(HANDLE pol, X_USTR *key,
                                         X_USTR *data)
{
    once(1, "it wanted to write the registry: skipped");
    return 0;
}

static BOOL WINAPI h_NotifyBootConfigStatus(BOOL good)
{
    once(2, "it wanted to report the boot as good: skipped");
    return TRUE;
}

static BOOL WINAPI h_RefreshPolicy(BOOL machine)
{
    return TRUE;
}

static BOOL WINAPI h_CreateProcessAsUserW(HANDLE tok, LPCWSTR app,
                                          LPWSTR cmd, LPVOID pa, LPVOID ta,
                                          BOOL inherit, DWORD flags,
                                          LPVOID env, LPCWSTR dir,
                                          LPVOID si, LPVOID pi)
{
    once(3, "it wanted to start a program: refused");
    SetLastError(ERROR_ACCESS_DENIED);
    return FALSE;
}

static LONG WINAPI h_NtShutdownSystem(int action)
{
    once(4, "it wanted to shut Windows down: refused");
    return (LONG) X_STATUS_ACCESS_DENIED;
}

static VOID WINAPI h_ExitProcess(UINT code)
{
    log_msg(LOG_WARN, "login: msgina.dll wanted to end ntrdps: only its "
            "thread ends; ntrdps's copy of the dialog from now on");
    g_state = -1;
    ExitThread(code);
}

static BOOL WINAPI h_TerminateProcess(HANDLE p, UINT code)
{
    if (p == GetCurrentProcess()) {
        h_ExitProcess(code);
    }
    once(3, "it wanted to end a program: refused");
    SetLastError(ERROR_ACCESS_DENIED);
    return FALSE;
}

static VOID WINAPI h_DebugBreak(void)
{
    once(7, "it stopped for a debugger: skipped");
}

static VOID WINAPI h_RtlAssert(PVOID what, PVOID file, ULONG line,
                               PCHAR text)
{
    log_msg(LOG_WARN, "login: msgina.dll: assertion failed: %.100s (%.60s, "
            "line %lu): skipped", what != NULL ? (char *) what : "",
            file != NULL ? (char *) file : "", (unsigned long) line);
}

static BOOL WINAPI h_SetThreadDesktop(HANDLE desk)
{
    if (desk != NULL && desk == g_desk) {
        return r_SetThreadDesktop(desk);
    }
    once(5, "it wanted another desktop: refused");
    SetLastError(ERROR_ACCESS_DENIED);
    return FALSE;
}

static int WINAPI h_MessageBoxW(HWND owner, LPCWSTR text, LPCWSTR title,
                                UINT style)
{
    if (!on_thread() || ending()) {
        once(6, "a window outside the dialog's thread: not shown");
        return IDCANCEL;
    }
    return r_MessageBoxW(owner, text, title, style);
}

static X_IPTR WINAPI h_DialogBoxParamW(HINSTANCE inst, LPCWSTR tmpl,
                                       HWND owner, X_DLGPROC proc,
                                       LPARAM param)
{
    if (!on_thread() || ending()) {
        once(6, "a window outside the dialog's thread: not shown");
        return IDCANCEL;
    }
    return (X_IPTR) show(inst, tmpl, NULL, owner, proc, param);
}

static HWND WINAPI h_CreateDialogParamW(HINSTANCE inst, LPCWSTR tmpl,
                                        HWND owner, X_DLGPROC proc,
                                        LPARAM param)
{
    if (!on_thread() || ending()) {
        once(6, "a window outside the dialog's thread: not shown");
        return NULL;
    }
    return r_CreateDialogParamW(inst, tmpl, owner, proc, param);
}

typedef struct {
    const char *dll, *name;
    FARPROC     hook;
    FARPROC    *real;
    FARPROC     addr;
    int         count;
} HOOK;

#define HK(dll, name) { dll, #name, (FARPROC) h_##name, \
                        (FARPROC *) &r_##name, NULL, 0 }

static HOOK g_hooks[] = {
    HK("KERNEL32.DLL", GetProfileIntW),
    HK("KERNEL32.DLL", GetProfileStringW),
    HK("KERNEL32.DLL", GetProfileStringA),
    HK("KERNEL32.DLL", GetPrivateProfileStringW),
    HK("KERNEL32.DLL", WriteProfileStringW),
    HK("KERNEL32.DLL", ExitProcess),
    HK("KERNEL32.DLL", TerminateProcess),
    HK("KERNEL32.DLL", DebugBreak),
    HK("ADVAPI32.DLL", RegQueryValueExW),
    HK("ADVAPI32.DLL", RegSetValueExW),
    HK("ADVAPI32.DLL", RegDeleteValueW),
    HK("ADVAPI32.DLL", RegDeleteKeyW),
    HK("ADVAPI32.DLL", RegCreateKeyExW),
    HK("ADVAPI32.DLL", LsaRetrievePrivateData),
    HK("ADVAPI32.DLL", LsaStorePrivateData),
    HK("ADVAPI32.DLL", NotifyBootConfigStatus),
    HK("ADVAPI32.DLL", CreateProcessAsUserW),
    HK("NTDLL.DLL",    NtShutdownSystem),
    HK("NTDLL.DLL",    RtlAssert),
    HK("USERENV.DLL",  RefreshPolicy),
    HK("USER32.DLL",   SetThreadDesktop),
    HK("USER32.DLL",   MessageBoxW),
    HK("USER32.DLL",   DialogBoxParamW),
    HK("USER32.DLL",   CreateDialogParamW)
};

#define NHOOKS (sizeof(g_hooks) / sizeof(g_hooks[0]))

static int set_slot(FARPROC *slot, unsigned int k)
{
    DWORD old, tmp;

    if (*slot == g_hooks[k].hook) {
        return 1;
    }
    if (!VirtualProtect((LPVOID) slot, sizeof(FARPROC),
                        PAGE_EXECUTE_READWRITE, &old)) {
        return 0;
    }
    if (*g_hooks[k].real == NULL) {
        *g_hooks[k].real = *slot;
    }
    *slot = g_hooks[k].hook;
    VirtualProtect((LPVOID) slot, sizeof(FARPROC), old, &tmp);
    g_hooks[k].count++;
    return 1;
}

static int patch(HMODULE m)
{
    const X_UPTR ordinal = (X_UPTR) 1 << (sizeof(X_UPTR) * 8 - 1);
    unsigned char *base = (unsigned char *) m;
    unsigned char *nt;
    DWORD *desc, rva;
    X_UPTR *names;
    FARPROC *slots;
    const char *dll, *fn;
    unsigned int i, k;
    HMODULE h;

    for (k = 0; k < NHOOKS; k++) {
        h = GetModuleHandleA(g_hooks[k].dll);
        g_hooks[k].addr = h != NULL ? GetProcAddress(h, g_hooks[k].name)
                                    : NULL;
        if (*g_hooks[k].real == NULL) {
            *g_hooks[k].real = g_hooks[k].addr;
        }
    }
    if (*(WORD *) base != 0x5A4D) {
        return 0;
    }
    nt = base + *(LONG *) (base + 0x3C);
    if (*(DWORD *) nt != 0x00004550) {
        return 0;
    }
    rva = *(DWORD *) (nt + 24 + (*(WORD *) (nt + 24) == 0x20B ? 112 : 96) +
                      8);
    if (rva == 0) {
        return 0;
    }
    for (desc = (DWORD *) (base + rva); desc[3] != 0; desc += 5) {
        dll = (const char *) (base + desc[3]);
        names = desc[0] != 0 ? (X_UPTR *) (base + desc[0]) : NULL;
        slots = (FARPROC *) (base + desc[4]);
        for (i = 0; slots[i] != NULL; i++) {
            fn = names != NULL && names[i] != 0 && !(names[i] & ordinal)
                 ? (const char *) (base + (DWORD) names[i] + 2) : NULL;
            for (k = 0; k < NHOOKS; k++) {
                if ((fn != NULL && lstrcmpiA(dll, g_hooks[k].dll) == 0 &&
                     strcmp(fn, g_hooks[k].name) == 0) ||
                    (g_hooks[k].addr != NULL &&
                     slots[i] == g_hooks[k].addr)) {
                    if (!set_slot(&slots[i], k)) {
                        log_msg(LOG_WARN, "login: msgina.dll: its call of "
                                "%s cannot go through ntrdps (error %lu)",
                                g_hooks[k].name,
                                (unsigned long) GetLastError());
                        return 0;
                    }
                    break;
                }
            }
        }
    }
    return 1;
}

static VOID WINAPI w_use_cad(HANDLE wlx)
{
}

static VOID WINAPI w_set_context(HANDLE wlx, PVOID ctx)
{
    g_wlx = ctx;
}

static VOID WINAPI w_sas_notify(HANDLE wlx, DWORD sas)
{
}

static BOOL WINAPI w_set_timeout(HANDLE wlx, DWORD seconds)
{
    return TRUE;
}

static int WINAPI w_assign_shell(HANDLE wlx, HANDLE tok, HANDLE proc,
                                 HANDLE thread)
{
    return ERROR_ACCESS_DENIED;
}

static int WINAPI w_message_box(HANDLE wlx, HWND owner, LPWSTR text,
                                LPWSTR title, UINT style)
{
    if (!on_thread() || ending()) {
        return IDCANCEL;
    }
    return r_MessageBoxW != NULL ? r_MessageBoxW(owner, text, title, style)
                                 : MessageBoxW(owner, text, title, style);
}

static int WINAPI w_dialog_box(HANDLE wlx, HANDLE inst, LPWSTR tmpl,
                               HWND owner, X_DLGPROC proc)
{
    return show(inst, tmpl, NULL, owner, proc, 0);
}

static int WINAPI w_dialog_box_param(HANDLE wlx, HANDLE inst, LPWSTR tmpl,
                                     HWND owner, X_DLGPROC proc,
                                     LPARAM param)
{
    return show(inst, tmpl, NULL, owner, proc, param);
}

static int WINAPI w_dialog_box_indirect(HANDLE wlx, HANDLE inst,
                                        LPDLGTEMPLATEW t, HWND owner,
                                        X_DLGPROC proc)
{
    return show(inst, NULL, t, owner, proc, 0);
}

static int WINAPI w_dialog_box_indirect_param(HANDLE wlx, HANDLE inst,
                                              LPDLGTEMPLATEW t, HWND owner,
                                              X_DLGPROC proc, LPARAM param)
{
    return show(inst, NULL, t, owner, proc, param);
}

static int WINAPI w_switch(HANDLE wlx)
{
    return 0;
}

static int WINAPI w_change_pw(HANDLE wlx, PVOID mpr, DWORD info)
{
    return 0;
}

static BOOL WINAPI w_get_source_desktop(HANDLE wlx, X_DESKTOP **pp)
{
    const char *name = g_host != NULL ? g_host->desk_name : "";
    unsigned int n = (unsigned int) strlen(name), i;
    X_DESKTOP *d;
    HANDLE dup = NULL;

    if (pp == NULL || g_desk == NULL ||
        !DuplicateHandle(GetCurrentProcess(), g_desk, GetCurrentProcess(),
                         &dup, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
        return FALSE;
    }
    d = (X_DESKTOP *) LocalAlloc(LPTR, sizeof(X_DESKTOP) +
                                       (n + 1) * sizeof(WCHAR));
    if (d == NULL) {
        CloseHandle(dup);
        return FALSE;
    }
    d->size = sizeof(X_DESKTOP);
    d->flags = X_WLX_DESKTOP_NAME | X_WLX_DESKTOP_HANDLE;
    d->desk = dup;
    d->name = (PWSTR) (d + 1);
    for (i = 0; i <= n; i++) {
        d->name[i] = (WCHAR) (unsigned char) name[i];
    }
    *pp = d;
    return TRUE;
}

static BOOL WINAPI w_set_return_desktop(HANDLE wlx, X_DESKTOP *d)
{
    return TRUE;
}

static BOOL WINAPI w_create_user_desktop(HANDLE wlx, HANDLE tok, DWORD flags,
                                         PWSTR name, X_DESKTOP **pp)
{
    SetLastError(ERROR_ACCESS_DENIED);
    return FALSE;
}

static int WINAPI w_change_pw_ex(HANDLE wlx, PVOID mpr, DWORD info,
                                 PWSTR provider, PVOID reserved)
{
    return 0;
}

static BOOL WINAPI w_close_user_desktop(HANDLE wlx, X_DESKTOP *d,
                                        HANDLE tok)
{
    return TRUE;
}

static BOOL WINAPI w_set_option(HANDLE wlx, DWORD opt, X_UPTR value,
                                X_UPTR *old)
{
    if (opt == X_WLX_OPTION_CONTEXT_POINTER) {
        if (old != NULL) {
            *old = (X_UPTR) g_wlx;
        }
        g_wlx = (PVOID) value;
        return TRUE;
    }
    if (opt >= X_WLX_OPTION_USE_CTRL_ALT_DEL &&
        opt <= X_WLX_OPTION_FORCE_LOGOFF_TIME) {
        if (old != NULL) {
            *old = g_opt[opt];
        }
        g_opt[opt] = value;
        return TRUE;
    }
    return FALSE;
}

static BOOL WINAPI w_get_option(HANDLE wlx, DWORD opt, X_UPTR *value)
{
    if (value == NULL) {
        return FALSE;
    }
    switch (opt) {
    case X_WLX_OPTION_CONTEXT_POINTER:
        *value = (X_UPTR) g_wlx;
        return TRUE;
    case X_WLX_OPTION_USE_CTRL_ALT_DEL:
    case X_WLX_OPTION_USE_SMART_CARD:
    case X_WLX_OPTION_FORCE_LOGOFF_TIME:
        *value = g_opt[opt];
        return TRUE;
    case X_WLX_OPTION_SMART_CARD_PRESENT:
    case X_WLX_OPTION_SMART_CARD_INFO:
        *value = 0;
        return TRUE;
    case X_WLX_OPTION_TABLE_SIZE:
        *value = (X_UPTR) ((g_ver >= X_WLX_VERSION_1_4 ? N_1_4 : N_1_3) *
                           sizeof(FARPROC));
        return TRUE;
    }
    return FALSE;
}

static VOID WINAPI w_win31_migrate(HANDLE wlx)
{
}

static BOOL WINAPI w_query_credentials(PVOID cred)
{
    return FALSE;
}

static BOOL WINAPI w_disconnect(void)
{
    return FALSE;
}

static BOOL WINAPI w_query_ts_data(HANDLE wlx, PVOID data, PWSTR user,
                                   PWSTR domain)
{
    return FALSE;
}

static X_UPTR WINAPI w_none(PVOID cred)
{
    return 0;
}

static void make_table(void)
{
    unsigned int i;

    g_table.use_ctrl_alt_del = (FARPROC) w_use_cad;
    g_table.set_context_pointer = (FARPROC) w_set_context;
    g_table.sas_notify = (FARPROC) w_sas_notify;
    g_table.set_timeout = (FARPROC) w_set_timeout;
    g_table.assign_shell_protection = (FARPROC) w_assign_shell;
    g_table.message_box = (FARPROC) w_message_box;
    g_table.dialog_box = (FARPROC) w_dialog_box;
    g_table.dialog_box_param = (FARPROC) w_dialog_box_param;
    g_table.dialog_box_indirect = (FARPROC) w_dialog_box_indirect;
    g_table.dialog_box_indirect_param = (FARPROC) w_dialog_box_indirect_param;
    g_table.switch_to_user = (FARPROC) w_switch;
    g_table.switch_to_winlogon = (FARPROC) w_switch;
    g_table.change_password_notify = (FARPROC) w_change_pw;
    g_table.get_source_desktop = (FARPROC) w_get_source_desktop;
    g_table.set_return_desktop = (FARPROC) w_set_return_desktop;
    g_table.create_user_desktop = (FARPROC) w_create_user_desktop;
    g_table.change_password_notify_ex = (FARPROC) w_change_pw_ex;
    g_table.close_user_desktop = (FARPROC) w_close_user_desktop;
    g_table.set_option = (FARPROC) w_set_option;
    g_table.get_option = (FARPROC) w_get_option;
    g_table.win31_migrate = (FARPROC) w_win31_migrate;
    g_table.query_client_credentials = (FARPROC) w_query_credentials;
    g_table.query_ic_credentials = (FARPROC) w_query_credentials;
    g_table.disconnect = (FARPROC) w_disconnect;
    g_table.query_ts_data = (FARPROC) w_query_ts_data;
    for (i = 0; i < EXTRA; i++) {
        g_table.extra[i] = (FARPROC) w_none;
    }
    g_opt[X_WLX_OPTION_USE_CTRL_ALT_DEL] = 1;
}

static int is_logon(HWND w)
{
    HWND pw = GetDlgItem(w, ID_PW);
    char cls[16];

    return GetDlgItem(w, ID_USER) != NULL && pw != NULL &&
           GetClassNameA(pw, cls, sizeof(cls)) &&
           lstrcmpiA(cls, "Edit") == 0 &&
           (GetWindowLongA(pw, GWL_STYLE) & ES_PASSWORD) != 0;
}

static void margins(HWND dlg)
{
    HWND c;
    char cls[16];

    for (c = GetWindow(dlg, GW_CHILD); c != NULL;
         c = GetWindow(c, GW_HWNDNEXT)) {
        if (GetClassNameA(c, cls, sizeof(cls)) &&
            lstrcmpiA(cls, "Edit") == 0) {
            SendMessageW(c, X_EM_SETMARGINS, X_EC_BOTH,
                         MAKELPARAM(X_EC_USEFONTINFO, X_EC_USEFONTINFO));
        }
    }
}

static X_IPTR CALLBACK wrap(HWND w, UINT m, WPARAM wp, LPARAM lp)
{
    CALL *c = (CALL *) GetPropA(w, PROP_NAME);
    GINA_HOST *h = g_host;
    X_IPTR r;

    if (c == NULL) {
        if (m != WM_INITDIALOG) {
            c = g_pending;
            return c != NULL ? c->proc(w, m, wp, lp) : FALSE;
        }
        c = (CALL *) lp;
        SetPropA(w, PROP_NAME, (HANDLE) c);
        g_pending = NULL;
        margins(w);
        r = c->proc(w, m, wp, c->param);
        if (h != NULL && is_logon(w)) {
            c->logon = 1;
            g_shown = 1;
            h->dialog(h->ctx, w, 1);
            r = FALSE;
        }
        return r;
    }
    if (m == WM_COMMAND && c->logon && h != NULL) {
        if (h->command(h->ctx, w, wp)) {
            return TRUE;
        }
        r = c->proc(w, m, wp, lp);
        if (IsWindow(w)) {
            h->dialog(h->ctx, w, 0);
        }
        return r;
    }
    r = c->proc(w, m, wp, lp);
    if (m == WM_NCDESTROY) {
        RemovePropA(w, PROP_NAME);
    }
    return r;
}

static void *look3d(HANDLE inst, LPCWSTR tmpl)
{
    HRSRC r = FindResourceW((HMODULE) inst, tmpl,
                            (LPCWSTR) (X_UPTR) 5 );
    HGLOBAL g = r != NULL ? LoadResource((HMODULE) inst, r) : NULL;
    DWORD n = r != NULL ? SizeofResource((HMODULE) inst, r) : 0;
    unsigned char *t;
    DWORD *style;

    if (g == NULL || n < 18 || LockResource(g) == NULL ||
        (t = (unsigned char *) LocalAlloc(LMEM_FIXED, n)) == NULL) {
        return NULL;
    }
    memcpy(t, LockResource(g), n);
    style = (DWORD *) (t + ((((WORD *) t)[0] == 1 &&
                             ((WORD *) t)[1] == 0xFFFF) ? 12 : 0));
    *style |= 0x0004L;
    return t;
}

static int show(HANDLE inst, LPCWSTR tmpl, LPDLGTEMPLATEW t, HWND owner,
                X_DLGPROC proc, LPARAM param)
{
    CALL c, *before = g_pending;
    void *copy;
    X_IPTR r;

    if (!on_thread() || ending() || proc == NULL) {
        return X_WLX_DLG_INPUT_TIMEOUT;
    }
    copy = t == NULL ? look3d(inst, tmpl) : NULL;
    c.proc = proc;
    c.param = param;
    c.logon = 0;
    g_pending = &c;
    if (copy != NULL || t != NULL) {
        r = DialogBoxIndirectParamW((HINSTANCE) inst, copy != NULL
                                    ? (LPDLGTEMPLATEW) copy : t, owner,
                                    (DLGPROC) wrap, (LPARAM) &c);
    } else if (r_DialogBoxParamW != NULL) {
        r = r_DialogBoxParamW((HINSTANCE) inst, tmpl, owner, wrap,
                              (LPARAM) &c);
    } else {
        r = DialogBoxParamW((HINSTANCE) inst, tmpl, owner, (DLGPROC) wrap,
                            (LPARAM) &c);
    }
    g_pending = before;
    if (copy != NULL) {
        LocalFree(copy);
    }
    return (int) r;
}

static BOOL CALLBACK end_one(HWND w, LPARAM lp)
{
    char cls[16];

    if (GetClassNameA(w, cls, sizeof(cls)) && lstrcmpA(cls, "#32770") == 0) {
        EndDialog(w, X_WLX_DLG_INPUT_TIMEOUT);
    }
    return TRUE;
}

void gina_end_all(void)
{
    if (!g_ending) {
        g_end_ms = GetTickCount();
    }
    g_ending = 1;
    EnumThreadWindows(GetCurrentThreadId(), (WNDENUMPROC) end_one, 0);
    PostQuitMessage(0);
}

HMODULE gina_load(void)
{
    HMODULE m = NULL;
    UINT n;

    if ((GetVersion() & 0xFF) != 5) {
        return NULL;
    }
    lock();
    if (g_state == 0) {
        n = GetSystemDirectoryA(g_path, MAX_PATH);
        if (n > 0 && n < MAX_PATH) {
            strcat(g_path, "\\msgina.dll");
            g_mod = LoadLibraryA(g_path);
        }
        g_state = g_mod != NULL ? 1 : -1;
        if (g_mod == NULL) {
            log_msg(LOG_WARN, "login: msgina.dll does not load (error %lu)",
                    (unsigned long) GetLastError());
        }
    }
    m = g_mod;
    unlock();
    return m;
}

static void init(void)
{
    G_NEGOTIATE p_neg;
    G_INITIALIZE p_init;
    DWORD ver = 0, t0 = GetTickCount();
    unsigned int k, n = 0;

    g_state = -1;
    p_neg = (G_NEGOTIATE) GetProcAddress(g_mod, "WlxNegotiate");
    p_init = (G_INITIALIZE) GetProcAddress(g_mod, "WlxInitialize");
    g_logged_out = (G_LOGGED_OUT) GetProcAddress(g_mod, "WlxLoggedOutSAS");
    if (p_neg == NULL || p_init == NULL || g_logged_out == NULL) {
        log_msg(LOG_WARN, "login: msgina.dll has no WlxNegotiate, "
                "WlxInitialize or WlxLoggedOutSAS");
        return;
    }
    if (!patch(g_mod)) {
        log_msg(LOG_WARN, "login: msgina.dll's import table cannot be "
                "changed: ntrdps's copy of the dialog");
        return;
    }
    for (k = 0; k < NHOOKS; k++) {
        n += (unsigned int) g_hooks[k].count;
    }
    make_table();
    if (!p_neg((DWORD) X_WLX_VERSION_1_4, &ver)) {
        log_msg(LOG_WARN, "login: msgina.dll refuses Winlogon version 1.4 "
                "(it has %lu.%lu)", (unsigned long) (ver >> 16),
                (unsigned long) (ver & 0xFFFF));
        return;
    }
    g_ver = ver;
    if (!p_init(g_winsta, (HANDLE) &g_table, NULL, (PVOID) &g_table,
                &g_wlx) || g_wlx == NULL) {
        log_msg(LOG_WARN, "login: msgina.dll's WlxInitialize failed "
                "(error %lu)", (unsigned long) GetLastError());
        return;
    }
    g_state = 2;
    log_msg(LOG_INFO, "login: msgina.dll (GINA %lu.%lu) is ready in %lu ms; "
            "%u of its calls to Windows go through ntrdps",
            (unsigned long) (ver >> 16), (unsigned long) (ver & 0xFFFF),
            (unsigned long) (GetTickCount() - t0), n);
}

static void free_out(HANDLE token, X_MPR *mpr, X_PROFILE *prof)
{
    if (token != NULL) {
        CloseHandle(token);
    }
    if (mpr->pw != NULL) {
        memset(mpr->pw, 0, (size_t) lstrlenW(mpr->pw) * sizeof(WCHAR));
    }
    if (mpr->old_pw != NULL) {
        memset(mpr->old_pw, 0, (size_t) lstrlenW(mpr->old_pw) *
                               sizeof(WCHAR));
    }
    if (mpr->user != NULL) {
        LocalFree(mpr->user);
    }
    if (mpr->domain != NULL) {
        LocalFree(mpr->domain);
    }
    if (mpr->pw != NULL) {
        LocalFree(mpr->pw);
    }
    if (mpr->old_pw != NULL) {
        LocalFree(mpr->old_pw);
    }
    if (prof == NULL) {
        return;
    }
    if (prof->profile != NULL) {
        LocalFree(prof->profile);
    }
    if (prof->type == X_WLX_PROFILE_TYPE_V2_0) {
        if (prof->policy != NULL) {
            LocalFree(prof->policy);
        }
        if (prof->net_default != NULL) {
            LocalFree(prof->net_default);
        }
        if (prof->server != NULL) {
            LocalFree(prof->server);
        }
        if (prof->env != NULL) {
            LocalFree(prof->env);
        }
    }
    LocalFree(prof);
}

int gina_run(GINA_HOST *h)
{
    LUID luid;
    DWORD opts = (DWORD) X_WLX_OPTION_IGNORE_AUTO_LOGON;
    HANDLE token = NULL;
    X_MPR mpr;
    PVOID prof = NULL;
    int r, ok, shown, ended;
    DWORD t0;

    if ((GetVersion() & 0xFF) != 5 || g_mod == NULL || g_state < 0) {
        return GINA_NO;
    }
    if (InterlockedExchange(&g_busy, 1) != 0) {
        log_msg(LOG_INFO, "session %s: login: another login has msgina.dll's "
                "dialog: ntrdps's copy of it", h->peer);
        return GINA_NO;
    }
    g_host = h;
    g_tid = GetCurrentThreadId();
    g_desk = h->desk;
    g_ending = 0;
    g_shown = 0;
    g_pending = NULL;
    t0 = GetTickCount();
    lock();
    if (GetTickCount() - t0 >= 2000) {
        log_msg(LOG_WARN, "session %s: login: waited %lu ms for msgina.dll",
                h->peer, (unsigned long) (GetTickCount() - t0));
    }
    if (g_state == 1) {
        init();
    }
    ok = g_state == 2;
    unlock();
    if (!ok) {
        g_host = NULL;
        g_tid = 0;
        InterlockedExchange(&g_busy, 0);
        return GINA_NO;
    }
    memset(g_sid, 0, sizeof(g_sid));
    ((BYTE *) g_sid)[0] = 1;
    ((BYTE *) g_sid)[1] = 3;
    ((BYTE *) g_sid)[7] = 5;
    g_sid[2] = 5;
    g_sid[4] = GetTickCount() ^ GetCurrentThreadId();
    memset(&luid, 0, sizeof(luid));
    memset(&mpr, 0, sizeof(mpr));
    log_msg(LOG_INFO, "session %s: login: msgina.dll shows its own logon "
            "dialog (Windows 2000)", h->peer);
    r = g_logged_out(g_wlx, X_WLX_SAS_TYPE_CTRL_ALT_DEL, &luid,
                     (PSID) g_sid, &opts, &token, (PVOID) &mpr, &prof);
    shown = g_shown;
    ended = ending();
    if (g_ending && GetTickCount() - g_end_ms >= 2000) {
        log_msg(LOG_WARN, "session %s: login: msgina.dll took %lu ms to "
                "end its dialog", h->peer,
                (unsigned long) (GetTickCount() - g_end_ms));
    }
    free_out(token, &mpr, (X_PROFILE *) prof);
    g_host = NULL;
    g_tid = 0;
    g_pending = NULL;
    InterlockedExchange(&g_busy, 0);
    if (ended) {
        return GINA_SHOWN;
    }
    if (!shown) {
        log_msg(LOG_WARN, "session %s: login: msgina.dll returned %d "
                "without its logon dialog: ntrdps's copy of it", h->peer, r);
        return GINA_NO;
    }
    g_state = -1;
    log_msg(LOG_WARN, "session %s: login: msgina.dll ended its logon dialog "
            "by itself (%d%s): this login ends; ntrdps's copy of the "
            "dialog from now on", h->peer, r, r == X_WLX_SAS_ACTION_LOGON
            ? ", a logon of its own: not used" : "");
    return GINA_LOST;
}
