/* Built-in read-only battery protocols. Included after the existing transports.
 * Protocol references: HaloBattery providers/{razer,hyperx,steelseries}.py (MIT).
 * No configuration files, downloaded code, or unknown-device command probing.
 */
typedef struct {
    HIDD_ATTRIBUTES attributes;
    HIDP_CAPS caps;
    PHIDP_PREPARSED_DATA preparsed;
    const WCHAR *path, *name, *key;
} DeviceInfo;
typedef struct { int percent, charging, approximate; } Reading;
typedef struct {
    BYTE request[128], prefix[8];
    DWORD requestLength, prefixLength, minimumReply, levelOffset;
    int stateOffset, onlineState, chargingOffset, chargingValue, scale;
    BOOL skipZeroReportId;
    unsigned allowedStatusMask, chargingMask;
} PacketRule;
static BOOL parsePacket(const PacketRule *rule,const BYTE *bytes,DWORD length,Reading *reading) {
    if(rule->skipZeroReportId && length && bytes[0]==0) {bytes++;length--;}
    if(length<rule->minimumReply || rule->prefixLength>length || rule->levelOffset>=length || memcmp(bytes,rule->prefix,rule->prefixLength)) return FALSE;
    if(rule->stateOffset>=0 && ((DWORD)rule->stateOffset>=length || bytes[rule->stateOffset]!=rule->onlineState)) return FALSE;
    int raw=bytes[rule->levelOffset];
    if(rule->scale<=0 || raw>rule->scale) return FALSE;
    if(rule->chargingOffset>=0 && (DWORD)rule->chargingOffset>=length) return FALSE;
    if(rule->allowedStatusMask && (rule->chargingOffset<0 || bytes[rule->chargingOffset]>=32 || !(rule->allowedStatusMask&(1u<<bytes[rule->chargingOffset])))) return FALSE;
    reading->percent=raw*100/rule->scale;
    reading->charging=rule->chargingOffset>=0 && bytes[rule->chargingOffset]==rule->chargingValue;
    if(rule->chargingMask && rule->chargingOffset>=0) reading->charging=(rule->chargingMask&(1u<<bytes[rule->chargingOffset]))!=0;
    reading->approximate=rule->scale!=100;
    return TRUE;
}
static BOOL queryPacket(HANDLE h,const HIDP_CAPS *caps,const PacketRule *rule,Reading *reading) {
    BYTE tx[128]={0},rx[128]={0};DWORD done;
    if(caps->OutputReportByteLength<rule->requestLength || caps->OutputReportByteLength>sizeof(tx) || !caps->InputReportByteLength || caps->InputReportByteLength>sizeof(rx)) return FALSE;
    memcpy(tx,rule->request,rule->requestLength);
    if(!transfer(h,TRUE,tx,caps->OutputReportByteLength,&done,500) || done!=caps->OutputReportByteLength) return FALSE;
    ULONGLONG end=GetTickCount64()+1000;
    while(GetTickCount64()<end) {
        ULONGLONG now=GetTickCount64();if(now>=end) break;
        if(!transfer(h,FALSE,rx,caps->InputReportByteLength,&done,(DWORD)(end-now))) break;
        if(parsePacket(rule,rx,done,reading)) return TRUE;
    }
    return FALSE;
}
static HANDLE openProtocol(const DeviceInfo *d) {
    return CreateFileW(d->path,GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,FILE_FLAG_OVERLAPPED,NULL);
}
static const PacketRule hyperxLevel={{6,0xff,0xbb,2},{6,0xff,0xbb,2},52,4,20,7,-1,0,-1,0,100,FALSE,0,0};
static const PacketRule hyperxCharge={{6,0xff,0xbb,3},{6,0xff,0xbb,3},52,4,20,4,-1,0,-1,0,1,FALSE,0,0};
static const PacketRule nova7Level={{0,0xb0},{0xb0},2,1,4,2,1,3,3,1,100,TRUE,0x0e,0x06};
static BOOL matchesHyperx(const DeviceInfo *d) {
    return d->attributes.VendorID==0x03f0 && (d->attributes.ProductID==0x0696 || d->attributes.ProductID==0x018b) && d->caps.UsagePage==0xff90 && d->caps.Usage==0x0303;
}
static BOOL matchesNova7(const DeviceInfo *d) {
    if(d->attributes.VendorID!=0x1038 || d->caps.UsagePage!=0xffc0 || d->caps.Usage!=1) return FALSE;
    switch(d->attributes.ProductID) {
    case 0x22a1:case 0x227e:case 0x2258:case 0x229e:case 0x22ad:case 0x22a5:case 0x22a9:case 0x22a7:case 0x2298:
    case 0x2202:case 0x2206:case 0x22a4:case 0x223a:case 0x227a:case 0x220a:return TRUE;
    default:return FALSE;
    }
}
static BOOL readHyperx(HANDLE metadata,const DeviceInfo *d,Snapshot *s) {
    (void)metadata;HANDLE h=openProtocol(d);if(h==INVALID_HANDLE_VALUE) return FALSE;
    Reading r={0},charge={0};BOOL ok=queryPacket(h,&d->caps,&hyperxLevel,&r);
    if(ok && queryPacket(h,&d->caps,&hyperxCharge,&charge)) r.charging=charge.percent==100;
    CloseHandle(h);if(ok) add(s,d->key,d->name,r.percent,r.charging,0);return ok;
}
static BOOL readNova7(HANDLE metadata,const DeviceInfo *d,Snapshot *s) {
    (void)metadata;HANDLE h=openProtocol(d);if(h==INVALID_HANDLE_VALUE) return FALSE;
    PacketRule rule=nova7Level;
    switch(d->attributes.ProductID) {case 0x2202:case 0x2206:case 0x22a4:case 0x223a:case 0x227a:case 0x220a:rule.scale=4;break;}
    Reading r={0};BOOL ok=queryPacket(h,&d->caps,&rule,&r);CloseHandle(h);
    /* Nova 7 status 0 means off; 1/2 charging, 3 on battery. */
    if(ok) add(s,d->key,d->name,r.percent,r.charging,r.approximate);
    return ok;
}
typedef struct { USHORT pid;BYTE transaction; } RazerModel;
static const RazerModel razerModels[]={
    {0x007a,0xff},{0x007b,0xff}, /* Viper Ultimate */
    {0x007c,0x3f},{0x007d,0x3f}, /* DeathAdder V2 Pro */
    {0x0086,0x1f},{0x0088,0x1f}, /* Basilisk Ultimate */
    {0x0094,0x1f},{0x009a,0x1f}, /* Orochi V2, Pro Click Mini */
    {0x00a5,0x1f},{0x00a6,0x1f}, /* Viper V2 Pro */
    {0x00aa,0x1f},{0x00ab,0x1f}, /* Basilisk V3 Pro */
    {0x00b6,0x1f},{0x00b7,0x1f}, /* DeathAdder V3 Pro */
    {0x00c0,0x1f},{0x00c1,0x1f}  /* Viper V3 Pro */
};
static const RazerModel *razerModel(const DeviceInfo *d) {
    if(d->attributes.VendorID!=0x1532 || d->caps.FeatureReportByteLength!=91) return NULL;
    for(DWORD i=0;i<_countof(razerModels);i++) if(razerModels[i].pid==d->attributes.ProductID) return &razerModels[i];
    return NULL;
}
static BOOL matchesRazer(const DeviceInfo *d) {return razerModel(d)!=NULL;}
static void razerRequest(BYTE *data,BYTE tid,BYTE cmd) {
    memset(data,0,91);data[2]=tid;data[6]=2;data[7]=7;data[8]=cmd;
    BYTE crc=0;for(int i=3;i<=88;i++) crc^=data[i];data[89]=crc;
}
static BOOL parseRazer(const BYTE *data,DWORD length,BYTE tid,BYTE cmd,int *value) {
    if(length!=91 || data[0]!=0 || data[1]!=2 || data[2]!=tid || data[6]!=2 || data[7]!=7 || data[8]!=cmd) return FALSE;
    BYTE crc=0;for(int i=3;i<=88;i++) crc^=data[i];if(crc!=data[89]) return FALSE;
    *value=data[10];return TRUE;
}
static BOOL razerQuery(HANDLE h,BYTE tid,BYTE cmd,int *value) {
    BYTE data[91];razerRequest(data,tid,cmd);
    if(!HidD_SetFeature(h,data,sizeof(data))) return FALSE;
    for(int i=0;i<8;i++) {
        if(WaitForSingleObject(stopEvent,80)!=WAIT_TIMEOUT) return FALSE;
        memset(data,0,sizeof(data));
        if(!HidD_GetFeature(h,data,sizeof(data))) return FALSE;
        if(parseRazer(data,sizeof(data),tid,cmd,value)) return TRUE;
    }
    return FALSE;
}
static BOOL readRazer(HANDLE metadata,const DeviceInfo *d,Snapshot *s) {
    (void)metadata;
    const RazerModel *m=razerModel(d);int raw,charge=0;
    if(!m) return FALSE;
    HANDLE h=openProtocol(d);if(h==INVALID_HANDLE_VALUE) return FALSE;
    if(!razerQuery(h,m->transaction,0x80,&raw)) {CloseHandle(h);return FALSE;}
    razerQuery(h,m->transaction,0x84,&charge);
    CloseHandle(h);
    add(s,d->key,d->name,(raw*100+127)/255,charge!=0,0);return TRUE;
}
static BOOL matchesGanss(const DeviceInfo *d) {
    return d->attributes.VendorID==0x05ac && d->attributes.ProductID==0x024f && wcsstr(d->name,L"GS3104T-PRO") && d->caps.UsagePage==0xff60 && d->caps.Usage==0x61;
}
static BOOL readGanss(HANDLE metadata,const DeviceInfo *d,Snapshot *s) {
    (void)metadata;HANDLE h=openProtocol(d);if(h==INVALID_HANDLE_VALUE) return FALSE;
    BOOL ok=ganss(h,(HIDP_CAPS*)&d->caps,d->name,s);CloseHandle(h);return ok;
}
static BOOL matchesLogitech(const DeviceInfo *d) {
    return d->attributes.VendorID==0x046d && d->caps.UsagePage==0xff00 && d->caps.Usage==2;
}
static BOOL readLogitech(HANDLE metadata,const DeviceInfo *d,Snapshot *s) {
    (void)metadata;HANDLE h=openProtocol(d);if(h==INVALID_HANDLE_VALUE) return FALSE;
    int before=s->count;logitech(h,(HIDP_CAPS*)&d->caps,d->path,d->name,s);CloseHandle(h);return s->count>before;
}
static BOOL matchesStandard(const DeviceInfo *d) {
    return d->caps.NumberFeatureValueCaps>0 && d->caps.FeatureReportByteLength>0;
}
static BOOL readStandard(HANDLE h,const DeviceInfo *d,Snapshot *s) {
    return standardBattery(h,d->preparsed,(HIDP_CAPS*)&d->caps,d->path,d->name,s);
}
typedef struct {
    const WCHAR *name,*description;BOOL (*matches)(const DeviceInfo*);BOOL (*read)(HANDLE,const DeviceInfo*,Snapshot*);
} BatteryProvider;
static const BatteryProvider providers[]={
    {L"GANSS",L"GS3104T-PRO 05AC:024F (local hardware verified)",matchesGanss,readGanss},
    {L"HID++",L"Logitech HID++ 2.0 1004/1000 (PRO X locally verified)",matchesLogitech,readLogitech},
    {L"Razer",L"16 Razer USB IDs / 9 mouse models (protocol only; not locally tested)",matchesRazer,readRazer},
    {L"HyperX",L"Cloud II Wireless 03F0:0696/018B (not locally tested)",matchesHyperx,readHyperx},
    {L"Nova7",L"15 Nova 7/7x/7P USB IDs (not locally tested)",matchesNova7,readNova7},
    {L"StandardHID",L"Descriptor-declared HID Battery Strength feature (not locally tested)",matchesStandard,readStandard}
};
static void dispatchBattery(HANDLE h,const DeviceInfo *d,Snapshot *s) {
    const WCHAR *matched=NULL;
    for(DWORD i=0;i<_countof(providers);i++) {
        const BatteryProvider *p=&providers[i];if(!p->matches(d)) continue;
        if(p->matches!=matchesStandard) matched=p->name;
        int before=s->count;
        if(p->read(h,d,s)) {
            logline(L"  provider=%s: valid battery reply",p->name);
            /* A GANSS reading may update the placeholder of an earlier collection. */
            for(int j=0;j<s->count;j++) if(j>=before || !wcscmp(s->items[j].key,d->key)) wcsncpy_s(s->items[j].protocol,_countof(s->items[j].protocol),p->name,_TRUNCATE);
            if(p->matches==matchesGanss) for(int j=0;j<s->count;j++) if(!wcscmp(s->items[j].key,L"GANSS-GS3104T-PRO")) wcscpy_s(s->items[j].protocol,_countof(s->items[j].protocol),p->name);
            return;
        }
    }
    if(matched || wcsstr(d->name,L"GS3104") || wcsstr(d->name,L"2.4G") || wcsstr(d->name,L"GANSS")) {
        const WCHAR *key=wcsstr(d->name,L"GS3104")?L"GANSS-GS3104T-PRO":d->key;
        for(int j=0;j<s->count;j++) if(!wcscmp(s->items[j].key,key)) {
            if(matched && s->items[j].percent<0) wcscpy_s(s->items[j].protocol,_countof(s->items[j].protocol),matched);
            return;
        }
        if(s->count==MAX_DEV) return;
        add(s,key,d->name,-1,0,0);Battery *b=&s->items[s->count-1];
        wcscpy_s(b->protocol,_countof(b->protocol),matched?matched:L"Unknown");
        logline(L"  provider=%s: no valid reading",b->protocol);
    }
}
