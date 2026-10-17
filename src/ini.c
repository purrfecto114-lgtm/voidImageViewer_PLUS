//
// Copyright 2025 voidtools / David Carpenter
// 
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
//
// load and sort ini files

#include "viv.h"

// types
// an ini key to load.
typedef struct _ini_keyvalue_s
{
	// next key.
	// this is wasted memory after converting to a binary tree.
	union
	{
		struct _ini_keyvalue_s *next;
		int index;
	};

	// key
	utf8_t *key;
	
	// value
	utf8_t *value;
	
}_ini_keyvalue_t;

// an ini file.
// an ini can not have zero items.
// ini_open returns zero when no items exist.
typedef struct _ini_s
{
	// number of keys
	int count;
	
	// keyvalue array
	_ini_keyvalue_t **indexes;

}_ini_t;

static int _ini_keyvalue_compare(const void *va,const void *vb);
static _ini_keyvalue_t *_ini_keyvalue_find(ini_t *ini,const utf8_t *key);

// the outcome of the last ini_open, and the anomaly mask beside it. both
// are file statics on purpose: nothing here becomes an exported global, so
// no other translation unit's symbol can collide with the reporting.
static int _ini_last_state = INI_LOAD_OK;
static int _ini_last_anomaly = INI_ANOMALY_NONE;

// the field ceiling. a real settings file carries numbers, short flags and
// paths: the widest value this writer produces is a MAX_PATH in utf-8 (a
// little over a kilobyte) and the key names are ascii. 64 kb is four
// decades of headroom over that, so anything past it is a corrupted or a
// planted line. the reader still copies it - it stays tolerant - but it
// records the anomaly so the caller can ask before it overwrites.
#define _INI_MAX_FIELD 65536

// the head ceiling of the pre-check: a real ini is a few kilobytes, so a
// file past this is not a settings file and the full parse answers an
// anomaly of its own anyway.
#define _INI_PRECHECK_MAX_BYTES 65536

int ini_get_last_state(void)
{
	return _ini_last_state;
}

int ini_get_last_anomaly(void)
{
	return _ini_last_anomaly;
}

// is the span blank (spaces and tabs only)? a blank line inside a section
// is legal, so it must not be counted as a malformed key line.
static int _ini_line_is_blank(const utf8_t *p,int len)
{
	int i;
	
	for(i=0;i<len;i++)
	{
		if ((p[i] != ' ') && (p[i] != '\t'))
		{
			return 0;
		}
	}
	
	return 1;
}

int ini_precheck(const wchar_t *filename,const utf8_t *ascii_section)
{
	char *buf;
	DWORD numread;
	DWORD size;
	int found;
	HANDLE h;
	LARGE_INTEGER file_size;
	
	found = 0;
	
	h = CreateFile(filename,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,0,OPEN_EXISTING,FILE_FLAG_SEQUENTIAL_SCAN,0);
	if (h == INVALID_HANDLE_VALUE)
	{
		return 0;
	}
	
	if ((GetFileSizeEx(h,&file_size)) && (file_size.QuadPart > 0) && (file_size.QuadPart <= _INI_PRECHECK_MAX_BYTES))
	{
		size = (DWORD)file_size.QuadPart;
		
		buf = mem_alloc(safe_size_add_one(size));
		
		if (ReadFile(h,buf,size,&numread,NULL))
		{
			DWORD i;
			int has_nul;
			utf8_t *needle;
			DWORD needle_len;
			
			// null terminate (the scan below is over the payload only).
			buf[size] = 0;
			
			// an embedded nul hides everything after it from the line
			// walk: such a file cannot be a known-good source.
			has_nul = 0;
			
			for(i=0;i<size;i++)
			{
				if (!buf[i])
				{
					has_nul = 1;
					break;
				}
			}
			
			if (!has_nul)
			{
				// the section header, built once: '[' + name + ']'.
				needle_len = 2 + (DWORD)utf8_length(ascii_section);
				
				needle = mem_alloc(safe_size_add_one((SIZE_T)needle_len));
				
				needle[0] = '[';
				os_copy_memory(needle + 1,ascii_section,needle_len - 2);
				needle[needle_len - 1] = ']';
				needle[needle_len] = 0;
				
				for(i=0;(i + needle_len) <= size;i++)
				{
					// the byte walk: this tree's vocabulary has no memcmp.
					int same;
					DWORD j;

					same = 1;

					for(j=0;j<needle_len;j++)
					{
						if ((utf8_t)buf[i + j] != needle[j])
						{
							same = 0;
							break;
						}
					}

					if (same)
					{
						found = 1;
						break;
					}
				}

				mem_free(needle);
			}
		}

		mem_free(buf);
	}

	CloseHandle(h);
	
	return found;
}

ini_t *ini_open(const wchar_t *filename,const utf8_t *ascii_section)
{
	ini_t *ini;
	_ini_keyvalue_t *kv_start;
	_ini_keyvalue_t *kv_last;
	DWORD count;
	HANDLE h;
	int section_found;

	kv_start = 0;
	kv_last = 0;
	count = 0;
	ini = 0;
	section_found = 0;
	
	// every call starts from a clean report.
	_ini_last_state = INI_LOAD_OK;
	_ini_last_anomaly = INI_ANOMALY_NONE;
	
	h = CreateFile(filename,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,0,OPEN_EXISTING,FILE_FLAG_SEQUENTIAL_SCAN,0);
	if (h != INVALID_HANDLE_VALUE)
	{
		LARGE_INTEGER file_size;
		
		// GetFileSizeEx, not GetFileSize: the 32-bit form answers a
		// 4 gb-plus file with its low dword (INVALID_FILE_SIZE only when
		// that dword happens to be 0xffffffff), so a huge ini could slip
		// through as a small one. a real ini is a few kilobytes: anything
		// past 16 mb is a hostile or corrupted file, refuse it before the
		// allocation.
		if ((GetFileSizeEx(h,&file_size)) && (file_size.QuadPart > 0) && (file_size.QuadPart <= 0x1000000))
		{
			char *buf;
			DWORD size;
			DWORD numread;
			
			size = (DWORD)file_size.QuadPart;
			
			buf = mem_alloc(safe_size_add_one(size));
			
			if (ReadFile(h,buf,size,&numread,NULL))
			{
				if (numread == size)
				{
					const utf8_t *p;
					int is_section;

					// null terminate.
					buf[size] = 0;
					
					// an embedded nul hides the rest of the file from
					// every line walk below: the parse stops dead there and
					// the keys after it silently keep their compiled-in
					// defaults. the scan is over the payload only - the
					// terminator this line just wrote sits at buf[size].
					{
						DWORD nul_scan;

						for(nul_scan=0;nul_scan<size;nul_scan++)
						{
							if (!buf[nul_scan])
							{
								_ini_last_anomaly |= INI_ANOMALY_EMBEDDED_NUL;
								break;
							}
						}
					}
				
					is_section = 0;

					// skip UTF-8 header
					p = utf8_skip_utf8_header(buf);
					
					for(;;)
					{

next_line:
						
						if (*p == '[')
						{
							const utf8_t *ascii_section_p;
							
							// skip the '['
							p++;
							
							is_section = 0;
							ascii_section_p = ascii_section;
							
							for(;;)
							{
								if (!*ascii_section_p)
								{
									if (*p == ']') 
									{
										p++;
										
										is_section = 1;
									section_found = 1;
									}
									
									break;
								}
								
								if (*p == '\n')
								{
									p++;
									goto next_line;
								}

								if ((*p == '\r') && (p[1] == '\n'))
								{
									p+=2;
									goto next_line;
								}			
								
								// compare
								if (*ascii_section_p != *p)
								{
									break;
								}
								
								ascii_section_p++;
								p++;
							}
						}
						else
						if ((*p == '#') || (*p == ';'))
						{
							// ignore this line.
							p++;
						}
						else		
						if (is_section)
						{
							int key_len;
							const utf8_t *key_start;
							
							key_start = p;
							
							while(*p)
							{
								if (*p == '\n')
								{
									// a line that ends here never reached the '=': the key
									// line is malformed. a blank line is legal in an ini
									// and is not one.
									if (!_ini_line_is_blank(key_start,(int)(p - key_start)))
									{
										_ini_last_anomaly |= INI_ANOMALY_MALFORMED_LINE;
									}

									p++;
								}

								if ((*p == '\r') && (p[1] == '\n'))
								{
									// the same malformed exit on a crlf line.
									if (!_ini_line_is_blank(key_start,(int)(p - key_start)))
									{
										_ini_last_anomaly |= INI_ANOMALY_MALFORMED_LINE;
									}

									p+=2;
								}	
							
								if (*p == '=')
								{
									const utf8_t *value_start;
									int value_len;
									_ini_keyvalue_t *kv;
									
									key_len = (int)(p - key_start);

									// skip =
									p++;
									
									value_start = p;
									
									for(;;)
									{
										if (!*p)
										{
											value_len = (int)(p - value_start);

											break;
										}
										
										if (*p == '\n')
										{
											value_len = (int)(p - value_start);
											
											p++;

											break;
										}

										if ((*p == '\r') && (p[1] == '\n'))
										{
											value_len = (int)(p - value_start);
											
											p += 2;

											break;
										}						
										
										p++;
									}

									// a field past any real settings value is a
									// corrupted line. the reader still copies it - it
									// stays tolerant - but the caller hears about it
									// before it writes the file back.
									if ((key_len > _INI_MAX_FIELD) || (value_len > _INI_MAX_FIELD))
									{
																		_ini_last_anomaly |= INI_ANOMALY_OVERSIZED_VALUE;
									}

									// add a key / value pair.
									kv = mem_alloc(sizeof(_ini_keyvalue_t));
									
									// init
									kv->key = utf8_alloc_utf8_n(key_start,key_len);
									kv->value = utf8_alloc_utf8_n(value_start,value_len);

									// insert
									if (kv_start)
									{
										kv_last->next = kv;
									}
									else
									{
										kv_start = kv;
									}
									
									kv->next = 0;
									kv_last = kv;
									count++;
									
									goto next_line;
								}
								
								p++;

							// the scan only leaves here on the buffer terminator: a
							// last line with no '=' is as malformed as any other.
							if (!_ini_line_is_blank(key_start,(int)(p - key_start)))
							{
								_ini_last_anomaly |= INI_ANOMALY_MALFORMED_LINE;
							}
							}
						}
						
						// skip to next line
						for(;;)
						{
							if (!*p) 
							{
								goto done;
							}
							
							if (*p == '\n')
							{
								p++;
								
								goto next_line;
							}

							if ((*p == '\r') && (p[1] == '\n'))
							{
								p += 2;
								
								goto next_line;
							}
							
							p++;
						}
					}

				done:	 ;

				// the section header never appeared: whatever parsed
				// belongs to some other section, so every key the caller
				// wanted fell through to its compiled-in default and the
				// session is now running on defaults it will write back.
				if (!section_found)
				{
					_ini_last_anomaly |= INI_ANOMALY_SECTION_MISSING;
				}
				}
			}
			
			mem_free(buf);
		}
		else
		{
			// the two refusals the size gate answers. a zero-length file
			// held no settings at all (a fresh profile writes one on the
			// first exit), so it answers as "missing" and the exit path
			// asks nothing; a size the reader refuses, or a size it could
			// not read, answers as unreadable - a file the user must not
			// lose is very much not the same as no file.
			if ((GetFileSizeEx(h,&file_size)) && (file_size.QuadPart == 0))
			{
				_ini_last_state = INI_LOAD_FILE_MISSING;
			}
			else
			{
				_ini_last_state = INI_LOAD_UNREADABLE;
			}
		}

		CloseHandle(h);
	}
	else
	{
		// the open refusal is the one place the reader can say why it
		// found nothing, and the two answers mean opposite things to the
		// caller: "not there" is every fresh profile, "there and not
		// readable" is a file the user must not lose.
		DWORD open_error;

		open_error = GetLastError();

		if ((open_error == ERROR_FILE_NOT_FOUND) || (open_error == ERROR_PATH_NOT_FOUND))
		{
			_ini_last_state = INI_LOAD_FILE_MISSING;
		}
		else
		{
			_ini_last_state = INI_LOAD_UNREADABLE;
		}
	}
	

	if (count)
	{
		_ini_keyvalue_t **indexes;

		// alloc
		ini = mem_alloc(sizeof(_ini_t));
		
		// allocate indexes		
		indexes = mem_alloc(safe_size_mul_sizeof_pointer((SIZE_T)count));

		// fill in indexes
		// and conect next to index
		{		
			_ini_keyvalue_t **kv_d;
			_ini_keyvalue_t *kv;
			_ini_keyvalue_t *next_kv;
			int index;

			kv = kv_start;
			kv_d = indexes;
			index = 0;
			
			while(kv)
			{
				next_kv = kv->next;
				
				kv->index = index;

				*kv_d++ = kv;
				index++;
				
				kv = next_kv;
			}
		}
		
		// sort indexes
		os_qsort((void **)indexes,count,_ini_keyvalue_compare);
		
		// find and remove dupes
		{		
			_ini_keyvalue_t **kv_d;
			_ini_keyvalue_t **kv_e;
			_ini_keyvalue_t **kv_p;
			_ini_keyvalue_t *last;
			
			kv_d = indexes;
			kv_e = indexes + count;
			kv_p = indexes;
			last = 0;

			while(kv_p != kv_e)
			{
				if (last)
				{
					if (utf8_compare(last->key,(*kv_p)->key) == 0)
					{
							// the same key twice. the sort keeps the last one (the
							// ini contract), but two different values for one key is
							// a file disagreeing with itself - the shape a half
							// written or a merged file takes. identical values are
							// harmless, so only a real disagreement counts.
							if (utf8_compare(last->value,(*kv_p)->value) != 0)
							{
								_ini_last_anomaly |= INI_ANOMALY_DUPLICATE_KEY;
							}

						// free last
						mem_free(last->key);
						mem_free(last->value);
						mem_free(last);

						// replace last
						kv_d--;						
					}
				}

				*kv_d++ = *kv_p;
				last = *kv_p;
				kv_p++;
			}
			
			// calculate new count
			// count can not be zero.
			count = (uintptr_t)(kv_d - indexes);
		}
		
		// init ini
		ini->indexes = indexes;
		ini->count = count;
	}
	
	return ini;
}

void ini_close(ini_t *ini)
{
	// free
	{
		_ini_keyvalue_t **kv_p;
		_ini_keyvalue_t **kv_e;
		
		kv_p = ini->indexes;
		kv_e = ini->indexes + ini->count;
		
		while(kv_p != kv_e)
		{
			mem_free((*kv_p)->key);
			mem_free((*kv_p)->value);
			mem_free(*kv_p);
		
			kv_p++;
		}
	}	
	
	mem_free(ini->indexes);
	mem_free(ini);
}

static _ini_keyvalue_t *_ini_keyvalue_find(ini_t *ini,const utf8_t *key)
{
	int blo;
	int bhi;
	int bpos;
	int i;
	
	blo = 0;
	bhi = ini->count - 1;
	
	while(blo <= bhi)
	{
		_ini_keyvalue_t *kv;
		
		bpos = blo + ((bhi - blo) / 2);
		
		kv = ini->indexes[bpos];
		
		i = utf8_compare(key,kv->key);
		if (i < 0)
		{
			bhi = bpos - 1;
		}
		else
		if (!i)
		{
			return kv;
		}
		else
		{
			blo = bpos + 1;
		}
	}
	
	// not found
	return 0;
}

// compare two key values.
static int _ini_keyvalue_compare(const void *va,const void *vb)
{
	const _ini_keyvalue_t *a;
	const _ini_keyvalue_t *b;
	int i;

	a = (_ini_keyvalue_t *)va;
	b = (_ini_keyvalue_t *)vb;

	i = utf8_compare(a->key,b->key);
	if (i) return i;

	return a->index - b->index;
}

int ini_get_int(ini_t *ini,const utf8_t *key,int default_int_value)
{
	_ini_keyvalue_t *kv;
	
	// lookup key.
	kv = _ini_keyvalue_find(ini,key);
	if (kv)
	{
		return utf8_to_int(kv->value);
	}
	
	return default_int_value;
}

const utf8_t *ini_get_string(ini_t *ini,const utf8_t *key)
{
	_ini_keyvalue_t *kv;
	
	// lookup key.
	kv = _ini_keyvalue_find(ini,key);
	if (kv)
	{
		return kv->value;
	}
	
	return 0;
}
