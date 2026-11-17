#!/usr/bin/env python3
"""Logical pixel-storage ledger, not RSS, Private Bytes or physical VRAM.
The old GL_RGB internal format is unsized: its 3-byte texture figure is a
nominal RGB payload, not a driver allocation guarantee. Mipmaps are modeled
as 32-bit device-compatible bitmaps; codecs/queues/driver/window costs excluded.
"""
import argparse,json

def pot(n):return 1<<(n-1).bit_length()
def mip_bytes(w,h):
    total=0;depth=0
    while True:
        mw=max(1,(w+1)//(2<<depth));mh=max(1,(h+1)//(2<<depth))
        if mw==mh==1:break
        total+=mw*mh*4;depth+=1
    return total

def ledger(w,h,bpp=32):
    dib=((w*bpp+31)//32)*4*h;area=pot(w)*pot(h)
    stage=area*(3 if bpp==24 else 4);mips=mip_bytes(w,h)
    return {'width':w,'height':h,'source_bpp':bpp,'POT_width':pot(w),'POT_height':pot(h),
            'DIB_bytes':dib,'assumed_full_GDI_mip_bytes':mips,
            'old_GL_stage_bytes':stage,'old_GL_nominal_RGB_texture_bytes':area*3,
            'old_GL_CPU_payload_bytes':dib+mips+stage,
            'old_GL_CPU_plus_nominal_GPU_payload_bytes':dib+mips+stage+area*3,
            'old_D3D_managed_system_copy_bytes':area*4,'old_D3D_GPU_texture_payload_bytes':area*4,
            'old_D3D_CPU_payload_bytes':dib+mips+area*4,
            'old_D3D_CPU_plus_GPU_payload_bytes':dib+mips+area*8,
            'old_rotation_source_plus_3_RGBA_copies_bytes':dib+w*h*12,
            'new_NPOT_hardware_conservative_price_bytes':w*h*8,
            'new_hardware_admitted_x64':w*h*8<=512*1024**2,
            'new_hardware_admitted_x86':w*h*8<=128*1024**2,
            'units':{'MB':1000000,'MiB':1048576,'GB':1000000000,'GiB':1073741824},
            'warning':'Logical coexistence model only; no Windows memory or VRAM measurement.'}

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--wide',type=int,default=10000);p.add_argument('--high',type=int,default=10000);p.add_argument('--bpp',type=int,choices=[24,32],default=32);a=p.parse_args()
    if a.wide<=0 or a.high<=0:p.error('dimensions must be positive')
    print(json.dumps(ledger(a.wide,a.high,a.bpp),indent=2))
if __name__=='__main__':main()
