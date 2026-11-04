#include "../src/battery.c"
#include <assert.h>
int main(void) {
    BYTE response[33]={0,0x20,1,0,100};response[32]=0x85;
    assert(parseGanss(response,33)==100);
    assert(parseGanss(response,4)==-1);
    response[32]^=1;assert(parseGanss(response,33)==-1);
    response[32]^=1;response[1]=0x21;assert(parseGanss(response,33)==-1);
    response[1]=0x20;response[4]=101;response[32]=0x86;assert(parseGanss(response,33)==-1);
    response[4]=0;response[32]=0x21;assert(parseGanss(response,33)==-1);
    Snapshot s={0};add(&s,L"ganss",L"keyboard",-1,0,0);add(&s,L"ganss",L"keyboard",100,0,0);
    assert(s.count==1 && s.items[0].percent==100);
    puts("PASS: valid reply, truncated reply, checksum, command, range, offline sentinel, deduplication");
    /* Shared decoder must reject unrelated reports and offline stale values. */
    Reading reading={0};BYTE hx[20]={6,0xff,0xbb,2};hx[7]=73;
    assert(parsePacket(&hyperxLevel,hx,20,&reading) && reading.percent==73);
    assert(!parsePacket(&hyperxLevel,hx,7,&reading));
    hx[3]=3;assert(!parsePacket(&hyperxLevel,hx,20,&reading));hx[3]=2;
    hx[7]=101;assert(!parsePacket(&hyperxLevel,hx,20,&reading));hx[7]=0;
    assert(parsePacket(&hyperxLevel,hx,20,&reading) && reading.percent==0);
    BYTE nova[]={0xb0,3,68,3};
    assert(parsePacket(&nova7Level,nova,4,&reading) && reading.percent==68 && !reading.charging);
    nova[3]=2;assert(parsePacket(&nova7Level,nova,4,&reading) && reading.charging);
    nova[3]=0;assert(!parsePacket(&nova7Level,nova,4,&reading));nova[3]=3;
    nova[1]=2;assert(!parsePacket(&nova7Level,nova,4,&reading));nova[1]=3;
    BYTE prefixed[]={0,0xb0,3,68,3};assert(parsePacket(&nova7Level,prefixed,5,&reading));
    PacketRule discrete=nova7Level;discrete.scale=4;nova[2]=3;
    assert(parsePacket(&discrete,nova,4,&reading) && reading.percent==75 && reading.approximate);
    nova[2]=5;assert(!parsePacket(&discrete,nova,4,&reading));
    BYTE rz[91];razerRequest(rz,0x1f,0x80);assert(rz[7]==7 && rz[8]==0x80 && rz[89]==0x85);
    rz[1]=2;rz[10]=171;rz[89]^=171;int raw;
    assert(parseRazer(rz,91,0x1f,0x80,&raw) && raw==171);
    assert(!parseRazer(rz,90,0x1f,0x80,&raw));assert(!parseRazer(rz,91,0x3f,0x80,&raw));
    rz[89]^=1;assert(!parseRazer(rz,91,0x1f,0x80,&raw));rz[89]^=1;
    rz[1]=4;assert(!parseRazer(rz,91,0x1f,0x80,&raw));
    DeviceInfo device={0};device.name=L"GS3104T-PRO 2.4G";device.path=L"test";device.key=L"test";
    device.attributes.VendorID=0x05ac;device.attributes.ProductID=0x024f;device.caps.UsagePage=0xff60;device.caps.Usage=0x61;
    assert(matchesGanss(&device));device.name=L"Other keyboard";assert(!matchesGanss(&device));
    device.attributes.VendorID=0x03f0;device.attributes.ProductID=0x0696;device.caps.UsagePage=0xff90;device.caps.Usage=0x303;
    assert(matchesHyperx(&device));device.caps.Usage=1;assert(!matchesHyperx(&device));
    device.attributes.VendorID=0x1532;device.attributes.ProductID=0x00c1;device.caps.FeatureReportByteLength=91;
    assert(matchesRazer(&device));device.attributes.ProductID=0xffff;assert(!matchesRazer(&device));
    /* An unknown receiver is listed without sending any vendor query. */
    memset(&device,0,sizeof(device));device.name=L"Unknown 2.4G Receiver";device.path=L"unknown";device.key=L"unknown";
    Snapshot unknown={0};dispatchBattery(INVALID_HANDLE_VALUE,&device,&unknown);
    assert(unknown.count==1 && unknown.items[0].percent==-1 && !wcscmp(unknown.items[0].protocol,L"Unknown"));
    device.name=L"GS3104T-PRO 2.4G";device.path=L"";device.key=L"ganss";
    device.attributes.VendorID=0x05ac;device.attributes.ProductID=0x024f;
    Snapshot sleeping={0};dispatchBattery(INVALID_HANDLE_VALUE,&device,&sleeping);
    assert(sleeping.count==1 && !wcscmp(sleeping.items[0].protocol,L"Unknown"));
    device.caps.UsagePage=0xff60;device.caps.Usage=0x61;
    dispatchBattery(INVALID_HANDLE_VALUE,&device,&sleeping);
    assert(sleeping.count==1 && sleeping.items[0].percent==-1 && !wcscmp(sleeping.items[0].protocol,L"GANSS"));
    puts("PASS: shared packet decoder, offline/foreign reports, Razer CRC and transaction, exact device matching, unknown receiver fallback");
    int sizes[]={16,20,24,32},levels[]={100,99,39,8,0,-1};
    for(DWORD f=0;f<_countof(fontChoices);f++) for(int d=0;d<4;d++) for(int n=0;n<6;n++) {
        int side=sizes[d];HICON icon=renderIcon(levels[n],0,side,f);assert(icon);
        ICONINFO ii;assert(GetIconInfo(icon,&ii));BITMAP bm;assert(GetObjectW(ii.hbmColor,sizeof(bm),&bm));
        DWORD pixels[32*32];BITMAPINFO bi={0};bi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);bi.bmiHeader.biWidth=side;bi.bmiHeader.biHeight=-side;bi.bmiHeader.biPlanes=1;bi.bmiHeader.biBitCount=32;
        HDC dc=GetDC(NULL);assert(GetDIBits(dc,ii.hbmColor,0,side,pixels,&bi,DIB_RGB_COLORS));ReleaseDC(NULL,dc);
        int filled=0;for(int y=0;y<side;y++) for(int x=0;x<side;x++) {
            if(pixels[y*side+x]>>24) filled++;
            if(x==0 || y==0 || x==side-1 || y==side-1) assert((pixels[y*side+x]>>24)==0);
        }
        assert(filled>0);DeleteObject(ii.hbmColor);DeleteObject(ii.hbmMask);DestroyIcon(icon);
    }
    puts("PASS: 168 font/size/value combinations have visible glyphs and transparent borders (including 100)");
    return 0;
}
