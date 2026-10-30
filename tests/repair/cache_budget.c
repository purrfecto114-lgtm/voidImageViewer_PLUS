static size_t safe_size_mul(size_t a,size_t b){return b && a>SIZE_MAX/b?SIZE_MAX:a*b;}
static size_t safe_size_add(size_t a,size_t b){return a>SIZE_MAX-b?SIZE_MAX:a+b;}
/* PRODUCTION */
int main(void){assert(_viv_frame_set_bytes(1,8,1)>=56);assert(_viv_frame_set_bytes(1,8,2)>=112);assert(_viv_frame_set_bytes(8,1,1)>=56);assert(_viv_frame_set_bytes(8,8,1)>=336);assert(!_viv_frame_set_bytes(0,8,1));assert(_viv_frame_set_bytes(INT_MAX,INT_MAX,INT_MAX)==SIZE_MAX);puts("thin-image mip pricing and saturating overflow PASS");}
