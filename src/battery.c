#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0601
#include <windows.h>
#include <shellapi.h>
#include <setupapi.h>
#include <hidsdi.h>
#include <hidpi.h>
#include <stdio.h>
#include <wchar.h>
#include <stdarg.h>
#include <stdlib.h>

#define MAX_DEV 32
#define WM_RESULT (WM_APP+1)
#define WM_TRAY (WM_APP+2)
#define WM_STATUS (WM_APP+3)
#define WM_REFRESH (WM_APP+4)
#define INTERVAL 120000
typedef struct { WCHAR key[512], name[128]; int percent, charging, approximate; } Battery;
typedef struct { Battery items[MAX_DEV]; int count; } Snapshot;
static HWND window;
static HANDLE stopEvent, refreshEvent, worker;
static UINT taskbarCreated;
static Snapshot current;
static NOTIFYICONDATAW icons[MAX_DEV];
static int iconCount;
static int shellIcons;
typedef struct { const WCHAR *face; const WCHAR *label; int weight; } FontChoice;
static const FontChoice fontChoices[]={
    {L"Segoe UI Semibold",L"Segoe UI Semibold（简约）",FW_NORMAL},
    {L"Bahnschrift",L"Bahnschrift（推荐）",FW_SEMIBOLD},
    {L"DIN Next LT Pro Medium",L"DIN Next（紧凑）",FW_NORMAL},
    {L"MiSans",L"MiSans（柔和）",FW_MEDIUM},
    {L"Consolas",L"Consolas（等宽）",FW_BOLD},
    {L"Arial",L"Arial（清晰）",FW_BOLD},
    {L"Times New Roman",L"Times New Roman（衬线）",FW_BOLD}
};
static DWORD fontChoice=1;
static void fontSetting(BOOL save) {
    HKEY k;DWORD bytes=sizeof(fontChoice),type;
    if(RegCreateKeyExW(HKEY_CURRENT_USER,L"Software\\BatteryIndicator",0,NULL,0,KEY_QUERY_VALUE|KEY_SET_VALUE,NULL,&k,NULL)!=ERROR_SUCCESS) return;
    if(save) RegSetValueExW(k,L"Font",0,REG_DWORD,(BYTE*)&fontChoice,sizeof(fontChoice));
    else {DWORD value=1;if(RegQueryValueExW(k,L"Font",NULL,&type,(BYTE*)&value,&bytes)==ERROR_SUCCESS && type==REG_DWORD && value<_countof(fontChoices)) fontChoice=value;}
    RegCloseKey(k);
}
static FILE *report;
static const WCHAR *className=L"BatteryIndicator.Native.1";
static const WCHAR *runKey=L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

static void logline(const WCHAR *format, ...) {
    if (report) { va_list ap; va_start(ap,format); vfwprintf(report,format,ap); va_end(ap); fputws(L"\n",report); }
}
static void sibling(WCHAR *out, const WCHAR *name) {
    GetModuleFileNameW(NULL,out,MAX_PATH); WCHAR *p=wcsrchr(out,L'\\');
    if(p) p[1]=0; wcscat_s(out,MAX_PATH,name);
}
static int startup(BOOL set, BOOL enable) {
    HKEY k; WCHAR path[MAX_PATH], value[MAX_PATH+4]; DWORD size=sizeof(value), type;
    if(RegCreateKeyExW(HKEY_CURRENT_USER,runKey,0,NULL,0,KEY_QUERY_VALUE|KEY_SET_VALUE,NULL,&k,NULL)!=ERROR_SUCCESS) return -1;
    int result;
    if(set) {
        GetModuleFileNameW(NULL,path,MAX_PATH); swprintf_s(value,_countof(value),L"\"%s\"",path);
        LSTATUS status= enable ? RegSetValueExW(k,L"BatteryIndicator",0,REG_SZ,(BYTE*)value,(DWORD)((wcslen(value)+1)*sizeof(WCHAR))) : RegDeleteValueW(k,L"BatteryIndicator");
        result=(status==ERROR_SUCCESS || (!enable && status==ERROR_FILE_NOT_FOUND)) ? 0 : -1;
    } else result=RegQueryValueExW(k,L"BatteryIndicator",NULL,&type,(BYTE*)value,&size)==ERROR_SUCCESS;
    RegCloseKey(k); return result;
}
static void add(Snapshot *s,const WCHAR *key,const WCHAR *name,int percent,int charging,int approximate) {
    int i; for(i=0;i<s->count;i++) if(!wcscmp(s->items[i].key,key)) break;
    if(i==MAX_DEV) return;
    if(i==s->count) s->count++;
    Battery *b=&s->items[i]; wcsncpy_s(b->key,_countof(b->key),key,_TRUNCATE);
    wcsncpy_s(b->name,_countof(b->name),name,_TRUNCATE);
    b->percent=percent; b->charging=charging; b->approximate=approximate;
}
/* Every pending operation is cancelled AND completed before its stack buffer is released. */
static BOOL transfer(HANDLE h,BOOL writing,BYTE *buf,DWORD size,DWORD *done,DWORD timeout) {
    OVERLAPPED ov={0}; ov.hEvent=CreateEventW(NULL,TRUE,FALSE,NULL); if(!ov.hEvent) return FALSE;
    BOOL ok=writing ? WriteFile(h,buf,size,done,&ov) : ReadFile(h,buf,size,done,&ov);
    if(!ok && GetLastError()==ERROR_IO_PENDING) {
        HANDLE events[2]={stopEvent,ov.hEvent}; DWORD wait=WaitForMultipleObjects(2,events,FALSE,timeout);
        if(wait!=WAIT_OBJECT_0+1) CancelIoEx(h,&ov);
        ok=GetOverlappedResult(h,&ov,done,TRUE);
        if(wait!=WAIT_OBJECT_0+1) ok=FALSE;
    }
    CloseHandle(ov.hEvent); return ok;
}
static BOOL request(HANDLE h,USHORT inSize,USHORT outSize,BYTE slot,BYTE feature,BYTE function,const BYTE *params,int n,BYTE *reply) {
    static BYTE swid=9; swid=(BYTE)(swid==15?10:swid+1);
    BYTE tx[128]={0},rx[128]={0}; DWORD done;
    if(outSize<20 || outSize>sizeof(tx) || inSize<7 || inSize>sizeof(rx)) return FALSE;
    tx[0]=0x11;tx[1]=slot;tx[2]=feature;tx[3]=(BYTE)((function<<4)|swid);
    if(n>0) memcpy(tx+4,params,n);
    if(!transfer(h,TRUE,tx,outSize,&done,500) || done!=outSize) return FALSE;
    ULONGLONG end=GetTickCount64()+650;
    while(GetTickCount64()<end && WaitForSingleObject(stopEvent,0)!=WAIT_OBJECT_0) {
        ULONGLONG now=GetTickCount64();if(now>=end) break;
        if(!transfer(h,FALSE,rx,inSize,&done,(DWORD)(end-now))) break;
        if(done<7 || rx[1]!=slot) continue;
        if((rx[2]==0xff || rx[2]==0x8f) && rx[3]==feature && rx[4]==tx[3]) return FALSE;
        if(rx[2]==feature && rx[3]==tx[3]) {
            memset(reply,0,16); memcpy(reply,rx+4,min(done-4,16)); return TRUE;
        }
    }
    return FALSE;
}
static BYTE featureIndex(HANDLE h,HIDP_CAPS *caps,BYTE slot,USHORT feature) {
    BYTE args[2]={(BYTE)(feature>>8),(BYTE)feature},r[16];
    return request(h,caps->InputReportByteLength,caps->OutputReportByteLength,slot,0,0,args,2,r)?r[0]:0;
}
/* GANSS native driver 1.0.0.3: 0x4357a0 builds 00 20 01 ... 21;
   0x44f490 adds the checksum and reads 0x20 0x01 ... <percent>.
   Only this verified read command is sent to this exact model/collection. */
static int parseGanss(const BYTE *rx,DWORD size) {
    if(size!=33 || rx[0]!=0 || rx[1]!=0x20 || rx[2]!=1 || rx[4]==0 || rx[4]>100) return -1;
    BYTE sum=0;for(int i=0;i<32;i++) sum=(BYTE)(sum+rx[i]);
    return sum==rx[32]?rx[4]:-1;
}
static BOOL ganss(HANDLE h,HIDP_CAPS *caps,const WCHAR *name,Snapshot *s) {
    if(caps->OutputReportByteLength!=33 || caps->InputReportByteLength!=33) return FALSE;
    BYTE tx[33]={0},rx[33]={0};DWORD done;
    tx[1]=0x20;tx[2]=1;tx[32]=0x21;
    if(!transfer(h,TRUE,tx,33,&done,500) || done!=33) return FALSE;
    ULONGLONG end=GetTickCount64()+1000;
    while(GetTickCount64()<end) {
        ULONGLONG now=GetTickCount64();if(now>=end) break;
        if(!transfer(h,FALSE,rx,33,&done,(DWORD)(end-now))) break;
        if(report) {fputws(L"  GANSS reply:",report);for(DWORD b=0;b<done;b++) fwprintf(report,L" %02X",rx[b]);fputws(L"\n",report);}
        int percent=parseGanss(rx,done);
        if(percent>=0) {
            add(s,L"GANSS-GS3104T-PRO",name,percent,0,0);return TRUE;
        }
    }
    return FALSE;
}
static void logitech(HANDLE h,HIDP_CAPS *caps, const WCHAR *path,const WCHAR *product,Snapshot *s) {
    int slot; BOOL receiver=wcsstr(product,L"Receiver")!=NULL || wcsstr(product,L"receiver")!=NULL;
    for(slot=receiver?1:255;slot<=(receiver?6:255);slot++) {
        if(WaitForSingleObject(stopEvent,0)==WAIT_OBJECT_0) return;
        BYTE r[16],fi=featureIndex(h,caps,(BYTE)slot,0x1004); int unified=fi!=0;
        if(!fi) fi=featureIndex(h,caps,(BYTE)slot,0x1000);
        if(!fi) continue;
        BOOL ok=request(h,caps->InputReportByteLength,caps->OutputReportByteLength,(BYTE)slot,fi,(BYTE)(unified?1:0),NULL,0,r);
        if(!ok) continue;
        int percent=r[0],approx=0,charging=(r[2]>=1&&r[2]<=4);
        if(percent>100) continue;
        if(unified && percent==0 && r[1]!=0) {
            percent=r[1]==8?90:r[1]==4?50:r[1]==2?20:r[1]==1?5:-1; approx=1;
        }
        WCHAR name[128],key[512]; wcscpy_s(name,_countof(name),L"Logitech 无线设备");
        BYTE nf=featureIndex(h,caps,(BYTE)slot,5);
        if(nf && request(h,caps->InputReportByteLength,caps->OutputReportByteLength,(BYTE)slot,nf,0,NULL,0,r)) {
            int len=min(r[0],120),offset=0; char raw[128]={0};
            while(offset<len) { BYTE start=(BYTE)offset;
                if(!request(h,caps->InputReportByteLength,caps->OutputReportByteLength,(BYTE)slot,nf,1,&start,1,r)) break;
                int take=min(16,len-offset); memcpy(raw+offset,r,take); offset+=take;
            }
            if(offset==len && len>0) MultiByteToWideChar(CP_UTF8,0,raw,-1,name,_countof(name));
        }
        swprintf_s(key,_countof(key),L"%s:%d",path,slot);
        add(s,key,name,percent,charging,approx);
        logline(L"  HID++ slot=%d name=%s battery=%d%% charging=%d approximate=%d",slot,name,percent,charging,approx);
    }
}
/* Generic HID Battery Strength feature (Usage Page 0x06, Usage 0x20).
   Only descriptor-declared battery fields are read; no guessed vendor commands. */
static BOOL standardBattery(HANDLE h,PHIDP_PREPARSED_DATA pp,HIDP_CAPS *caps,const WCHAR *path,const WCHAR *name,Snapshot *s) {
    HIDP_VALUE_CAPS vc[128]; USHORT count=caps->NumberFeatureValueCaps;
    if(!count || count>128 || caps->FeatureReportByteLength>1024) return FALSE;
    if(HidP_GetValueCaps(HidP_Feature,vc,&count,pp)!=HIDP_STATUS_SUCCESS) return FALSE;
    for(USHORT i=0;i<count;i++) {
        if(vc[i].UsagePage!=6 || (vc[i].IsRange ? !(vc[i].Range.UsageMin<=0x20 && vc[i].Range.UsageMax>=0x20) : vc[i].NotRange.Usage!=0x20)) continue;
        BYTE data[1024]={0}; data[0]=vc[i].ReportID; ULONG value;
        if(HidD_GetFeature(h,data,caps->FeatureReportByteLength) && HidP_GetUsageValue(HidP_Feature,6,vc[i].LinkCollection,0x20,&value,pp,(PCHAR)data,caps->FeatureReportByteLength)==HIDP_STATUS_SUCCESS) {
            LONG lo=vc[i].LogicalMin,hi=vc[i].LogicalMax;
            if(hi>lo && (LONG)value>=lo && (LONG)value<=hi) {
                int percent=(int)(((LONGLONG)value-lo)*100/(hi-lo)); add(s,path,name,percent,0,hi!=100);
                logline(L"  HID Battery Strength=%d%% raw=%lu range=%ld..%ld",percent,value,lo,hi); return TRUE;
            }
        }
    }
    return FALSE;
}
static void scan(Snapshot *s) {
    GUID guid; HidD_GetHidGuid(&guid);
    HDEVINFO list=SetupDiGetClassDevsW(&guid,NULL,NULL,DIGCF_PRESENT|DIGCF_DEVICEINTERFACE);
    if(list==INVALID_HANDLE_VALUE) return;
    SP_DEVICE_INTERFACE_DATA iface={0}; iface.cbSize=sizeof(iface);
    for(DWORD i=0;SetupDiEnumDeviceInterfaces(list,NULL,&guid,i,&iface);i++) {
        if(WaitForSingleObject(stopEvent,0)==WAIT_OBJECT_0) break;
        DWORD size=0; SetupDiGetDeviceInterfaceDetailW(list,&iface,NULL,0,&size,NULL);
        PSP_DEVICE_INTERFACE_DETAIL_DATA_W detail=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,size);
        if(!detail) continue; detail->cbSize=sizeof(*detail);
        if(!SetupDiGetDeviceInterfaceDetailW(list,&iface,detail,size,NULL,NULL)) { HeapFree(GetProcessHeap(),0,detail); continue; }
        HANDLE h=CreateFileW(detail->DevicePath,0,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,0,NULL);
        if(h!=INVALID_HANDLE_VALUE) {
            HIDD_ATTRIBUTES attr={0};attr.Size=sizeof(attr); PHIDP_PREPARSED_DATA pp=NULL;HIDP_CAPS caps;
            WCHAR name[128]=L"HID 设备"; HidD_GetProductString(h,name,sizeof(name));
            if(HidD_GetAttributes(h,&attr) && HidD_GetPreparsedData(h,&pp)) {
                if(HidP_GetCaps(pp,&caps)==HIDP_STATUS_SUCCESS) {
                    logline(L"VID=%04X PID=%04X product=%s usage=%04X:%04X in=%u out=%u feature=%u",attr.VendorID,attr.ProductID,name,caps.UsagePage,caps.Usage,caps.InputReportByteLength,caps.OutputReportByteLength,caps.FeatureReportByteLength);
                    BOOL got=standardBattery(h,pp,&caps,detail->DevicePath,name,s);
                    if(attr.VendorID==0x05ac && attr.ProductID==0x024f && wcsstr(name,L"GS3104T-PRO") && caps.UsagePage==0xff60 && caps.Usage==0x61) {
                        HANDLE rw=CreateFileW(detail->DevicePath,GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,FILE_FLAG_OVERLAPPED,NULL);
                        if(rw!=INVALID_HANDLE_VALUE) {got=ganss(rw,&caps,name,s);CloseHandle(rw);}
                    }
                    if(attr.VendorID==0x046d && caps.UsagePage==0xff00 && caps.Usage==2) {
                        HANDLE rw=CreateFileW(detail->DevicePath,GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,FILE_FLAG_OVERLAPPED,NULL);
                        int before=s->count;
                        if(rw!=INVALID_HANDLE_VALUE) { logitech(rw,&caps,detail->DevicePath,name,s); CloseHandle(rw); }
                        if(before==s->count) add(s,detail->DevicePath,L"Logitech 接收器：设备休眠或无法读取",-1,0,0);
                    } else if(!got && (wcsstr(name,L"GS3104") || wcsstr(name,L"2.4G") || wcsstr(name,L"GANSS"))) {
                        WCHAR key[512]; wcsncpy_s(key,_countof(key),detail->DevicePath,_TRUNCATE);
                        /* Deduplicate composite collections of this known keyboard. */
                        if(wcsstr(name,L"GS3104")) wcscpy_s(key,_countof(key),L"GANSS-GS3104T-PRO");
                        BOOL existing=FALSE;for(int j=0;j<s->count;j++) if(!wcscmp(s->items[j].key,key)) existing=TRUE;
                        if(!existing) add(s,key,name,-1,0,0);
                    }
                }
                HidD_FreePreparsedData(pp);
            }
            CloseHandle(h);
        }
        HeapFree(GetProcessHeap(),0,detail);
    }
    SetupDiDestroyDeviceInfoList(list);
}
static DWORD WINAPI poller(void *unused) {
    (void)unused; HANDLE events[2]={stopEvent,refreshEvent};
    do {
        Snapshot *s=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,sizeof(Snapshot));
        if(s) { scan(s); if(!PostMessageW(window,WM_RESULT,0,(LPARAM)s)) HeapFree(GetProcessHeap(),0,s); }
    } while(WaitForMultipleObjects(2,events,FALSE,INTERVAL)!=WAIT_OBJECT_0);
    return 0;
}
static HICON renderIcon(int percent,int charging,int size,DWORD choice) {
    /* Render at 4x, fit the whole string, then average coverage into alpha.
       All three digits of 100 have a one-pixel margin at the native tray size. */
    const int scale=4;int side=size*scale;DWORD *hi,*pixels;
    BITMAPINFO bi={0};bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);bi.bmiHeader.biWidth=side;bi.bmiHeader.biHeight=-side;bi.bmiHeader.biPlanes=1;bi.bmiHeader.biBitCount=32;
    HDC dc=CreateCompatibleDC(NULL);HBITMAP high=CreateDIBSection(dc,&bi,DIB_RGB_COLORS,(void**)&hi,NULL,0);
    HGDIOBJ old=SelectObject(dc,high);memset(hi,0,side*side*4);
    WCHAR text[8];if(percent<0) wcscpy_s(text,8,L"?");else swprintf_s(text,8,L"%d",percent);
    HFONT font=NULL;HGDIOBJ oldFont=NULL;SIZE extent={0};int height=side-4*scale;
    if(choice>=_countof(fontChoices)) choice=1;
    do {
        if(font) {SelectObject(dc,oldFont);DeleteObject(font);}
        font=CreateFontW(-height,0,0,0,fontChoices[choice].weight,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,NONANTIALIASED_QUALITY,0,fontChoices[choice].face);
        oldFont=SelectObject(dc,font);GetTextExtentPoint32W(dc,text,(int)wcslen(text),&extent);height--;
    } while((extent.cx>side-2*scale || extent.cy>side-2*scale) && height>scale*5);
    SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(255,255,255));TextOutW(dc,0,0,text,(int)wcslen(text));GdiFlush();
    int left=side,top=side,right=-1,bottom=-1;
    for(int y=0;y<side;y++) for(int x=0;x<side;x++) if(hi[y*side+x]&0xffffff) {
        if(x<left) left=x;if(x>right) right=x;if(y<top) top=y;if(y>bottom) bottom=y;
    }
    int dx=(side-(right-left+1))/2-left,dy=(side-(bottom-top+1))/2-top;
    bi.bmiHeader.biWidth=size;bi.bmiHeader.biHeight=-size;
    HBITMAP bitmap=CreateDIBSection(dc,&bi,DIB_RGB_COLORS,(void**)&pixels,NULL,0);
    COLORREF color=percent<0?RGB(150,150,150):charging?RGB(70,210,140):percent<=20?RGB(255,90,80):RGB(255,45,149);
    for(int y=0;y<size;y++) for(int x=0;x<size;x++) {
        unsigned coverage=0;
        for(int sy=0;sy<scale;sy++) for(int sx=0;sx<scale;sx++) {
            int hx=x*scale+sx-dx,hy=y*scale+sy-dy;
            if(hx>=0&&hx<side&&hy>=0&&hy<side) coverage+=hi[hy*side+hx]&255;
        }
        unsigned a=(coverage+8)/16;
        pixels[y*size+x]=(a<<24)|((GetRValue(color)*a/255)<<16)|((GetGValue(color)*a/255)<<8)|(GetBValue(color)*a/255);
    }
    DWORD maskBytes=((size+15)/16)*2*size;BYTE *zeros=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,maskBytes);
    HBITMAP mask=CreateBitmap(size,size,1,1,zeros);HeapFree(GetProcessHeap(),0,zeros);
    ICONINFO ii={TRUE,0,0,mask,bitmap};HICON icon=CreateIconIndirect(&ii);
    SelectObject(dc,oldFont);SelectObject(dc,old);DeleteObject(font);DeleteObject(high);DeleteObject(mask);DeleteObject(bitmap);DeleteDC(dc);return icon;
}
static HICON makeIcon(int percent,int charging) {
    int size=GetSystemMetrics(SM_CXSMICON);return renderIcon(percent,charging,size<16?16:size,fontChoice);
}
static void clearIcons(void) {
    for(int i=0;i<iconCount;i++) { Shell_NotifyIconW(NIM_DELETE,&icons[i]); if(icons[i].hIcon) DestroyIcon(icons[i].hIcon); }
    iconCount=0;
    shellIcons=0;
}
static void showSnapshot(Snapshot *s) {
    /* No GDI allocations or shell updates while readings stay unchanged. */
    if(iconCount && !memcmp(&current,s,sizeof(*s))) return;
    clearIcons(); current=*s;
    if(!current.count) add(&current,L"none",L"未发现可读取电量的设备",-1,0,0);
    for(int i=0;i<current.count;i++) {
        Battery *b=&current.items[i]; NOTIFYICONDATAW *n=&icons[i]; memset(n,0,sizeof(*n));
        n->cbSize=sizeof(*n);n->hWnd=window;n->uID=i+1;n->uFlags=NIF_MESSAGE|NIF_ICON|NIF_TIP;n->uCallbackMessage=WM_TRAY;
        n->hIcon=makeIcon(b->percent,b->charging);
        if(b->percent<0) _snwprintf_s(n->szTip,_countof(n->szTip),_TRUNCATE,L"%s\n电量无法读取 / 协议未适配",b->name);
        else _snwprintf_s(n->szTip,_countof(n->szTip),_TRUNCATE,L"%s\n%s%d%%%s",b->name,b->approximate?L"约 ":L"",b->percent,b->charging?L" · 充电中":L"");
        if(Shell_NotifyIconW(NIM_ADD,n)) shellIcons++;
        iconCount++;
    }
}
static void menu(void) {
    HMENU m=CreatePopupMenu(); WCHAR line[200];
    for(int i=0;i<current.count;i++) {
        Battery *b=&current.items[i];
        if(b->percent<0) swprintf_s(line,_countof(line),L"%s — 无法读取",b->name);
        else swprintf_s(line,_countof(line),L"%s — %s%d%%%s",b->name,b->approximate?L"约 ":L"",b->percent,b->charging?L"（充电）":L"");
        AppendMenuW(m,MF_STRING|MF_DISABLED,0,line);
    }
    AppendMenuW(m,MF_SEPARATOR,0,NULL);AppendMenuW(m,MF_STRING,1,L"立即刷新（默认每 2 分钟）");
    AppendMenuW(m,MF_STRING|(startup(FALSE,FALSE)==1?MF_CHECKED:0),2,L"登录 Windows 时自动启动");
    HMENU fonts=CreatePopupMenu();
    for(DWORD i=0;i<_countof(fontChoices);i++) AppendMenuW(fonts,MF_STRING|(i==fontChoice?MF_CHECKED:0),100+i,fontChoices[i].label);
    AppendMenuW(m,MF_POPUP,(UINT_PTR)fonts,L"数字字体");
    AppendMenuW(m,MF_STRING,3,L"退出");POINT p;GetCursorPos(&p);SetForegroundWindow(window);
    int cmd=TrackPopupMenu(m,TPM_RETURNCMD|TPM_RIGHTBUTTON,p.x,p.y,0,window,NULL);DestroyMenu(m);PostMessageW(window,WM_NULL,0,0);
    if(cmd==1) SetEvent(refreshEvent);
    if(cmd==2 && startup(TRUE,startup(FALSE,FALSE)!=1)!=0) MessageBoxW(window,L"自启动设置失败。",L"BatteryIndicator",MB_ICONERROR);
    if(cmd==3) DestroyWindow(window);
    if(cmd>=100 && cmd<100+(int)_countof(fontChoices)) {fontChoice=(DWORD)(cmd-100);fontSetting(TRUE);Snapshot copy=current;clearIcons();showSnapshot(&copy);}
}
static LRESULT CALLBACK wndproc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
    if(msg==taskbarCreated && taskbarCreated) { iconCount=0; /* Explorer lost its icons, free our old handles. */
        for(int i=0;i<current.count;i++) {if(icons[i].hIcon) DestroyIcon(icons[i].hIcon);icons[i].hIcon=NULL;}
        Snapshot copy=current;showSnapshot(&copy);return 0;
    }
    switch(msg) {
    case WM_REFRESH: SetEvent(refreshEvent);return 0;
    case WM_STATUS: {
        WCHAR path[MAX_PATH];sibling(path,L"status.txt");FILE *f=NULL;
        _wfopen_s(&f,path,L"w, ccs=UTF-8");
        if(f) {fwprintf(f,L"Tray icons registered: %d/%d\n",shellIcons,iconCount);
            for(int i=0;i<current.count;i++) fwprintf(f,L"%s: %d%%\n",current.items[i].name,current.items[i].percent);
            fclose(f);
        }return 0;
    }
    case WM_RESULT: showSnapshot((Snapshot*)lp);HeapFree(GetProcessHeap(),0,(void*)lp);return 0;
    case WM_TRAY: if(lp==WM_RBUTTONUP || lp==WM_LBUTTONUP) menu();return 0;
    case WM_DEVICECHANGE: SetEvent(refreshEvent);return 0;
    case WM_POWERBROADCAST: if(wp==PBT_APMRESUMEAUTOMATIC) SetEvent(refreshEvent);return TRUE;
    case WM_CLOSE: DestroyWindow(hwnd);return 0;
    case WM_DESTROY: SetEvent(stopEvent);clearIcons();PostQuitMessage(0);return 0;
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE prev,PWSTR cmd,int show) {
    (void)prev;(void)show;
    if(!wcscmp(cmd,L"--install")) return startup(TRUE,TRUE)==0?0:1;
    if(!wcscmp(cmd,L"--uninstall")) return startup(TRUE,FALSE)==0?0:1;
    if(!wcscmp(cmd,L"--stop")) { HWND old=FindWindowW(className,NULL);if(old) PostMessageW(old,WM_CLOSE,0,0);return 0; }
    if(!wcscmp(cmd,L"--status") || !wcscmp(cmd,L"--refresh")) {
        HWND old=FindWindowW(className,NULL);if(!old) return 1;
        return SendMessageTimeoutW(old,!wcscmp(cmd,L"--status")?WM_STATUS:WM_REFRESH,0,0,SMTO_ABORTIFHUNG,2000,NULL)?0:1;
    }
    stopEvent=CreateEventW(NULL,TRUE,FALSE,NULL);refreshEvent=CreateEventW(NULL,FALSE,FALSE,NULL);
    if(!wcscmp(cmd,L"--probe")) {
        WCHAR path[MAX_PATH];sibling(path,L"diagnostics.txt"); _wfopen_s(&report,path,L"w, ccs=UTF-8");
        Snapshot s={0};scan(&s);if(report) fclose(report);CloseHandle(stopEvent);CloseHandle(refreshEvent);return s.count?0:2;
    }
    HANDLE mutex=CreateMutexW(NULL,FALSE,L"Local\\BatteryIndicator.Native.1");
    if(!mutex || GetLastError()==ERROR_ALREADY_EXISTS) {if(mutex) CloseHandle(mutex);return 0;}
    fontSetting(FALSE);SetProcessDPIAware();taskbarCreated=RegisterWindowMessageW(L"TaskbarCreated");
    WNDCLASSW wc={0};wc.lpfnWndProc=wndproc;wc.hInstance=instance;wc.lpszClassName=className;
    RegisterClassW(&wc);window=CreateWindowExW(0,className,L"BatteryIndicator",0,0,0,0,0,NULL,NULL,instance,NULL);
    if(!window) return 1;
    Snapshot initial={0};showSnapshot(&initial);
    worker=CreateThread(NULL,0,poller,NULL,0,NULL);if(!worker) {DestroyWindow(window);return 1;}
    MSG msg;while(GetMessageW(&msg,NULL,0,0)>0) {TranslateMessage(&msg);DispatchMessageW(&msg);}
    WaitForSingleObject(worker,INFINITE);CloseHandle(worker);
    while(PeekMessageW(&msg,NULL,WM_RESULT,WM_RESULT,PM_REMOVE)) HeapFree(GetProcessHeap(),0,(void*)msg.lParam);
    CloseHandle(stopEvent);CloseHandle(refreshEvent);CloseHandle(mutex);return 0;
}
