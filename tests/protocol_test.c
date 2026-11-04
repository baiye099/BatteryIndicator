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
