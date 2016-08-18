/*
 * Copyright (c) 1997 - 2017 Kungliga Tekniska Högskolan
 * (Royal Institute of Technology, Stockholm, Sweden).
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "krb5_locl.h"

#define KRB5_KT_VNO_1 1
#define KRB5_KT_VNO_2 2
#define KRB5_KT_VNO   KRB5_KT_VNO_2

#define KRB5_KT_FL_JAVA 1


/* file operations -------------------------------------------- */

struct fkt_data {
    char *filename;
    int flags;
};

static krb5_error_code
krb5_kt_ret_data(krb5_context context,
		 krb5_storage *sp,
		 krb5_data *data)
{
    krb5_error_code ret;
    krb5_ssize_t bytes;
    int16_t size;

    ret = krb5_ret_int16(sp, &size);
    if(ret)
	return ret;
    data->length = size;
    data->data = malloc(size);
    if (data->data == NULL)
	return krb5_enomem(context);
    bytes = krb5_storage_read(sp, data->data, size);
    if (bytes != size)
	return (bytes == -1) ? errno : KRB5_KT_END;
    return 0;
}

static krb5_error_code
krb5_kt_ret_string(krb5_context context,
		   krb5_storage *sp,
		   heim_general_string *data)
{
    krb5_error_code ret;
    krb5_ssize_t bytes;
    int16_t size;

    ret = krb5_ret_int16(sp, &size);
    if(ret)
	return ret;
    *data = malloc(size + 1);
    if (*data == NULL)
	return krb5_enomem(context);
    bytes = krb5_storage_read(sp, *data, size);
    (*data)[size] = '\0';
    if (bytes != size)
	return (bytes == -1) ? errno : KRB5_KT_END;
    return 0;
}

static krb5_error_code
krb5_kt_store_data(krb5_context context,
		   krb5_storage *sp,
		   krb5_data data)
{
    krb5_error_code ret;
    krb5_ssize_t bytes;

    ret = krb5_store_int16(sp, data.length);
    if (ret != 0)
        return ret;
    bytes = krb5_storage_write(sp, data.data, data.length);
    if (bytes != (int)data.length)
        return bytes == -1 ? errno : KRB5_KT_END;
    return 0;
}

static krb5_error_code
krb5_kt_store_string(krb5_storage *sp,
		     heim_general_string data)
{
    krb5_error_code ret;
    krb5_ssize_t bytes;
    size_t len = strlen(data);

    ret = krb5_store_int16(sp, len);
    if (ret != 0)
	return ret;
    bytes = krb5_storage_write(sp, data, len);
    if (bytes != (int)len)
        return bytes == -1 ? errno : KRB5_KT_END;
    return 0;
}

static krb5_error_code
krb5_kt_ret_keyblock(krb5_context context,
		     struct fkt_data *fkt,
		     krb5_storage *sp,
		     krb5_keyblock *p)
{
    int ret;
    int16_t tmp;

    ret = krb5_ret_int16(sp, &tmp); /* keytype + etype */
    if(ret)  {
	krb5_set_error_message(context, ret,
			       N_("Cant read keyblock from file %s", ""),
			       fkt->filename);
	return ret;
    }
    p->keytype = tmp;
    ret = krb5_kt_ret_data(context, sp, &p->keyvalue);
    if (ret)
	krb5_set_error_message(context, ret,
			       N_("Cant read keyblock from file %s", ""),
			       fkt->filename);
    return ret;
}

static krb5_error_code
krb5_kt_store_keyblock(krb5_context context,
		       struct fkt_data *fkt,
		       krb5_storage *sp,
		       krb5_keyblock *p)
{
    int ret;

    ret = krb5_store_int16(sp, p->keytype); /* keytype + etype */
    if(ret) {
	krb5_set_error_message(context, ret,
			       N_("Cant store keyblock to file %s", ""),
			       fkt->filename);
	return ret;
    }
    ret = krb5_kt_store_data(context, sp, p->keyvalue);
    if (ret)
	krb5_set_error_message(context, ret,
			       N_("Cant store keyblock to file %s", ""),
			       fkt->filename);
    return ret;
}


static krb5_error_code
krb5_kt_ret_principal(krb5_context context,
		      struct fkt_data *fkt,
		      krb5_storage *sp,
		      krb5_principal *princ)
{
    size_t i;
    int ret;
    krb5_principal p;
    int16_t len;

    ALLOC(p, 1);
    if(p == NULL)
	return krb5_enomem(context);

    ret = krb5_ret_int16(sp, &len);
    if(ret) {
	krb5_set_error_message(context, ret,
			       N_("Failed decoding length of "
				  "keytab principal in keytab file %s", ""),
			       fkt->filename);
	goto out;
    }
    if(krb5_storage_is_flags(sp, KRB5_STORAGE_PRINCIPAL_WRONG_NUM_COMPONENTS))
	len--;
    if (len < 0) {
	ret = KRB5_KT_END;
	krb5_set_error_message(context, ret,
			       N_("Keytab principal contains "
				  "invalid length in keytab %s", ""),
			       fkt->filename);
	goto out;
    }
    ret = krb5_kt_ret_string(context, sp, &p->realm);
    if(ret) {
	krb5_set_error_message(context, ret,
			       N_("Can't read realm from keytab: %s", ""),
			       fkt->filename);
	goto out;
    }
    p->name.name_string.val = calloc(len, sizeof(*p->name.name_string.val));
    if(p->name.name_string.val == NULL) {
	ret = krb5_enomem(context);
	goto out;
    }
    p->name.name_string.len = len;
    for(i = 0; i < p->name.name_string.len; i++){
	ret = krb5_kt_ret_string(context, sp, p->name.name_string.val + i);
	if(ret) {
	    krb5_set_error_message(context, ret,
				   N_("Can't read principal from "
				      "keytab: %s", ""),
				   fkt->filename);
	    goto out;
	}
    }
    if (krb5_storage_is_flags(sp, KRB5_STORAGE_PRINCIPAL_NO_NAME_TYPE))
	p->name.name_type = KRB5_NT_UNKNOWN;
    else {
	int32_t tmp32;
	ret = krb5_ret_int32(sp, &tmp32);
	p->name.name_type = tmp32;
	if (ret) {
	    krb5_set_error_message(context, ret,
				   N_("Can't read name-type from "
				      "keytab: %s", ""),
				   fkt->filename);
	    goto out;
	}
    }
    *princ = p;
    return 0;
out:
    krb5_free_principal(context, p);
    return ret;
}

static krb5_error_code
krb5_kt_store_principal(krb5_context context,
			krb5_storage *sp,
			krb5_principal p)
{
    size_t i;
    int ret;

    if(krb5_storage_is_flags(sp, KRB5_STORAGE_PRINCIPAL_WRONG_NUM_COMPONENTS))
	ret = krb5_store_int16(sp, p->name.name_string.len + 1);
    else
	ret = krb5_store_int16(sp, p->name.name_string.len);
    if(ret) return ret;
    ret = krb5_kt_store_string(sp, p->realm);
    if(ret) return ret;
    for(i = 0; i < p->name.name_string.len; i++){
	ret = krb5_kt_store_string(sp, p->name.name_string.val[i]);
	if(ret)
	    return ret;
    }
    if(!krb5_storage_is_flags(sp, KRB5_STORAGE_PRINCIPAL_NO_NAME_TYPE)) {
	ret = krb5_store_int32(sp, p->name.name_type);
	if(ret)
	    return ret;
    }

    return 0;
}

static krb5_error_code KRB5_CALLCONV
fkt_resolve(krb5_context context, const char *name, krb5_keytab id)
{
    struct fkt_data *d;

    d = malloc(sizeof(*d));
    if(d == NULL)
	return krb5_enomem(context);
    d->filename = strdup(name);
    if(d->filename == NULL) {
	free(d);
	return krb5_enomem(context);
    }
    d->flags = 0;
    id->data = d;
    return 0;
}

static krb5_error_code KRB5_CALLCONV
fkt_resolve_java14(krb5_context context, const char *name, krb5_keytab id)
{
    krb5_error_code ret;

    ret = fkt_resolve(context, name, id);
    if (ret == 0) {
	struct fkt_data *d = id->data;
	d->flags |= KRB5_KT_FL_JAVA;
    }
    return ret;
}

static krb5_error_code KRB5_CALLCONV
fkt_close(krb5_context context, krb5_keytab id)
{
    struct fkt_data *d = id->data;
    free(d->filename);
    free(d);
    return 0;
}

static krb5_error_code KRB5_CALLCONV
fkt_destroy(krb5_context context, krb5_keytab id)
{
    struct fkt_data *d = id->data;
    _krb5_erase_file(context, d->filename);
    return 0;
}

static krb5_error_code KRB5_CALLCONV
fkt_get_name(krb5_context context,
	     krb5_keytab id,
	     char *name,
	     size_t namesize)
{
    /* This function is XXX */
    struct fkt_data *d = id->data;
    strlcpy(name, d->filename, namesize);
    return 0;
}

static void
storage_set_flags(krb5_context context, krb5_storage *sp, int vno)
{
    int flags = 0;
    switch(vno) {
    case KRB5_KT_VNO_1:
	flags |= KRB5_STORAGE_PRINCIPAL_WRONG_NUM_COMPONENTS;
	flags |= KRB5_STORAGE_PRINCIPAL_NO_NAME_TYPE;
	flags |= KRB5_STORAGE_HOST_BYTEORDER;
	break;
    case KRB5_KT_VNO_2:
	break;
    default:
	krb5_warnx(context,
		   "storage_set_flags called with bad vno (%d)", vno);
    }
    krb5_storage_set_flags(sp, flags);
}

static krb5_error_code
fkt_start_seq_get_int(krb5_context context,
		      krb5_keytab id,
		      int flags,
		      int exclusive,
		      krb5_kt_cursor *c)
{
    int8_t pvno, tag;
    krb5_error_code ret;
    struct fkt_data *d = id->data;
    const char *stdio_mode = "rb";

    c->fd = open (d->filename, flags);
    if (c->fd < 0) {
	ret = errno;
	krb5_set_error_message(context, ret,
			       N_("keytab %s open failed: %s", ""),
			       d->filename, strerror(ret));
	return ret;
    }
    rk_cloexec(c->fd);
    ret = _krb5_xlock(context, c->fd, exclusive, d->filename);
    if (ret) {
	close(c->fd);
	return ret;
    }
    if ((flags & O_ACCMODE) == O_RDWR && (flags & O_APPEND))
        stdio_mode = "ab+";
    else if ((flags & O_ACCMODE) == O_RDWR)
        stdio_mode = "rb+";
    else if ((flags & O_ACCMODE) == O_WRONLY)
        stdio_mode = "wb";
    c->sp = krb5_storage_stdio_from_fd(c->fd, stdio_mode);
    if (c->sp == NULL) {
	close(c->fd);
	return krb5_enomem(context);
    }
    krb5_storage_set_eof_code(c->sp, KRB5_KT_END);
    ret = krb5_ret_int8(c->sp, &pvno);
    if(ret) {
	krb5_storage_free(c->sp);
	close(c->fd);
	krb5_clear_error_message(context);
	return ret;
    }
    if(pvno != 5) {
	krb5_storage_free(c->sp);
	close(c->fd);
	krb5_clear_error_message (context);
	return KRB5_KEYTAB_BADVNO;
    }
    ret = krb5_ret_int8(c->sp, &tag);
    if (ret) {
	krb5_storage_free(c->sp);
	close(c->fd);
	krb5_clear_error_message(context);
	return ret;
    }
    id->version = tag;
    storage_set_flags(context, c->sp, id->version);
    return 0;
}

static krb5_error_code KRB5_CALLCONV
fkt_start_seq_get(krb5_context context,
		  krb5_keytab id,
		  krb5_kt_cursor *c)
{
    return fkt_start_seq_get_int(context, id, O_RDONLY | O_BINARY | O_CLOEXEC, 0, c);
}

static krb5_error_code
fkt_next_entry_int(krb5_context context,
		   krb5_keytab id,
		   krb5_keytab_entry *entry,
		   krb5_kt_cursor *cursor,
		   off_t *start,
		   off_t *end)
{
    struct fkt_data *d = id->data;
    int32_t len;
    int ret;
    int8_t tmp8;
    int32_t tmp32;
    uint32_t utmp32;
    off_t pos, curpos;

    pos = krb5_storage_seek(cursor->sp, 0, SEEK_CUR);
loop:
    ret = krb5_ret_int32(cursor->sp, &len);
    if (ret)
	return ret;
    if(len < 0) {
	pos = krb5_storage_seek(cursor->sp, -len, SEEK_CUR);
	goto loop;
    }
    ret = krb5_kt_ret_principal (context, d, cursor->sp, &entry->principal);
    if (ret)
	goto out;
    ret = krb5_ret_uint32(cursor->sp, &utmp32);
    entry->timestamp = utmp32;
    if (ret)
	goto out;
    ret = krb5_ret_int8(cursor->sp, &tmp8);
    if (ret)
	goto out;
    entry->vno = (unsigned char)tmp8;
    ret = krb5_kt_ret_keyblock (context, d, cursor->sp, &entry->keyblock);
    if (ret)
	goto out;
    /* there might be a 32 bit kvno here
     * if it's zero, assume that the 8bit one was right,
     * otherwise trust the new value */
    curpos = krb5_storage_seek(cursor->sp, 0, SEEK_CUR);
    if(len + 4 + pos - curpos >= 4) {
	ret = krb5_ret_int32(cursor->sp, &tmp32);
    /* The length indicates that there could be a 32bit kvno value, 
     * but because we may have been inserted into a previously deleted
     * spot the extra length could be a lot larger than the 4 bytes necessary
     * to hold a 32bit value.  So if we have at least 4 bytes read it, and 
     * then see if the 32 bit value matches the 8bit kvno value */
    if ( (ret == 0) && (tmp32 != 0) && ((tmp32 % 256) == entry->vno) )
	    entry->vno = tmp32;
    }
    /* there might be a flags field here */
    if(len + 4 + pos - curpos >= 8) {
	ret = krb5_ret_uint32(cursor->sp, &utmp32);
	if (ret == 0)
	    entry->flags = utmp32;
    } else
	entry->flags = 0;

    entry->aliases = NULL;

    if(start) *start = pos;
    if(end) *end = pos + 4 + len;
 out:
    if (ret)
        krb5_kt_free_entry(context, entry);
    krb5_storage_seek(cursor->sp, pos + 4 + len, SEEK_SET);
    return ret;
}

static krb5_error_code KRB5_CALLCONV
fkt_next_entry(krb5_context context,
	       krb5_keytab id,
	       krb5_keytab_entry *entry,
	       krb5_kt_cursor *cursor)
{
    return fkt_next_entry_int(context, id, entry, cursor, NULL, NULL);
}

static krb5_error_code KRB5_CALLCONV
fkt_end_seq_get(krb5_context context,
		krb5_keytab id,
		krb5_kt_cursor *cursor)
{
    krb5_storage_free(cursor->sp);
    close(cursor->fd);
    return 0;
}

static krb5_error_code KRB5_CALLCONV
fkt_setup_keytab(krb5_context context,
		 krb5_keytab id,
		 krb5_storage *sp)
{
    krb5_error_code ret;
    ret = krb5_store_int8(sp, 5);
    if(ret)
	return ret;
    if(id->version == 0)
	id->version = KRB5_KT_VNO;
    return krb5_store_int8 (sp, id->version);
}

static krb5_error_code KRB5_CALLCONV
fkt_add_entry(krb5_context context,
	      krb5_keytab id,
	      krb5_keytab_entry *entry)
{
    int ret;
    int fd;
    krb5_storage *sp;
    krb5_ssize_t bytes;
    struct fkt_data *d = id->data;
    krb5_data keytab;
    int32_t len;

    fd = open (d->filename, O_RDWR | O_BINARY | O_CLOEXEC);
    if (fd < 0) {
	fd = open (d->filename, O_RDWR | O_CREAT | O_EXCL | O_BINARY | O_CLOEXEC, 0600);
	if (fd < 0) {
	    ret = errno;
	    krb5_set_error_message(context, ret,
				   N_("open(%s): %s", ""), d->filename,
				   strerror(ret));
	    return ret;
	}
	rk_cloexec(fd);

	ret = _krb5_xlock(context, fd, 1, d->filename);
	if (ret) {
	    close(fd);
	    return ret;
	}
	sp = krb5_storage_stdio_from_fd(fd, "wb+");
	krb5_storage_set_eof_code(sp, KRB5_KT_END);
	ret = fkt_setup_keytab(context, id, sp);
	if(ret) {
	    goto out;
	}
	storage_set_flags(context, sp, id->version);
    } else {
	int8_t pvno, tag;

	rk_cloexec(fd);

	ret = _krb5_xlock(context, fd, 1, d->filename);
	if (ret) {
	    close(fd);
	    return ret;
	}
	sp = krb5_storage_stdio_from_fd(fd, "wb+");
	krb5_storage_set_eof_code(sp, KRB5_KT_END);
	ret = krb5_ret_int8(sp, &pvno);
	if(ret) {
	    /* we probably have a zero byte file, so try to set it up
               properly */
	    ret = fkt_setup_keytab(context, id, sp);
	    if(ret) {
		krb5_set_error_message(context, ret,
				       N_("%s: keytab is corrupted: %s", ""),
				       d->filename, strerror(ret));
		goto out;
	    }
	    storage_set_flags(context, sp, id->version);
	} else {
	    if(pvno != 5) {
		ret = KRB5_KEYTAB_BADVNO;
		krb5_set_error_message(context, ret,
				       N_("Bad version in keytab %s", ""),
				       d->filename);
		goto out;
	    }
	    ret = krb5_ret_int8 (sp, &tag);
	    if (ret) {
		krb5_set_error_message(context, ret,
				       N_("failed reading tag from "
					  "keytab %s", ""),
				       d->filename);
		goto out;
	    }
	    id->version = tag;
	    storage_set_flags(context, sp, id->version);
	}
    }

    {
	krb5_storage *emem;
	emem = krb5_storage_emem();
	if(emem == NULL) {
	    ret = krb5_enomem(context);
	    goto out;
	}
	ret = krb5_kt_store_principal(context, emem, entry->principal);
	if(ret) {
	    krb5_set_error_message(context, ret,
				   N_("Failed storing principal "
				      "in keytab %s", ""),
				   d->filename);
	    krb5_storage_free(emem);
	    goto out;
	}
	ret = krb5_store_int32 (emem, entry->timestamp);
	if(ret) {
	    krb5_set_error_message(context, ret,
				   N_("Failed storing timpstamp "
				      "in keytab %s", ""),
				   d->filename);
	    krb5_storage_free(emem);
	    goto out;
	}
	ret = krb5_store_int8 (emem, entry->vno % 256);
	if(ret) {
	    krb5_set_error_message(context, ret,
				   N_("Failed storing kvno "
				      "in keytab %s", ""),
				   d->filename);
	    krb5_storage_free(emem);
	    goto out;
	}
	ret = krb5_kt_store_keyblock (context, d, emem, &entry->keyblock);
	if(ret) {
	    krb5_storage_free(emem);
	    goto out;
	}
	if ((d->flags & KRB5_KT_FL_JAVA) == 0) {
	    ret = krb5_store_int32 (emem, entry->vno);
	    if (ret) {
		krb5_set_error_message(context, ret,
				       N_("Failed storing extended kvno "
					  "in keytab %s", ""),
				       d->filename);
		krb5_storage_free(emem);
		goto out;
	    }
	    ret = krb5_store_uint32 (emem, entry->flags);
	    if (ret) {
		krb5_set_error_message(context, ret,
				       N_("Failed storing extended kvno "
					  "in keytab %s", ""),
				       d->filename);
		krb5_storage_free(emem);
		goto out;
	    }
	}

	ret = krb5_storage_to_data(emem, &keytab);
	krb5_storage_free(emem);
	if(ret) {
	    krb5_set_error_message(context, ret,
				   N_("Failed converting keytab entry "
				      "to memory block for keytab %s", ""),
				   d->filename);
	    goto out;
	}
    }

    while(1) {
        off_t here;

        here = krb5_storage_seek(sp, 0, SEEK_CUR);
        if (here == -1) {
            ret = errno;
            krb5_set_error_message(context, ret,
                                   N_("Failed writing keytab block "
                                      "in keytab %s: %s", ""),
                                   d->filename, strerror(ret));
            goto out;
        }
	ret = krb5_ret_int32(sp, &len);
	if (ret) {
            /* There could have been a partial length.  Recover! */
            (void) krb5_storage_truncate(sp, here);
	    len = keytab.length;
	    break;
	}
    /* Putting a smaller entry in a larger delete space has caused problems
     * later when we are trying to determine whether a 32bit kvno value is
     * stored as a part of this entry.  the issue will be solved in 
     * fkt_next_entry_int() */
	if(len < 0) {
	    len = -len;
	    if(len >= (int32_t)keytab.length) {
		krb5_storage_seek(sp, -4, SEEK_CUR);
		break;
	    }
	}
	krb5_storage_seek(sp, len, SEEK_CUR);
    }
    ret = krb5_store_int32(sp, len);
    if (ret != 0)
        goto out;
    bytes = krb5_storage_write(sp, keytab.data, keytab.length);
    if (bytes != keytab.length) {
	ret = bytes == -1 ? errno : KRB5_KT_END;
	krb5_set_error_message(context, ret,
			       N_("Failed writing keytab block "
				  "in keytab %s: %s", ""),
			       d->filename, strerror(ret));
    }
    memset(keytab.data, 0, keytab.length);
    krb5_data_free(&keytab);
  out:
    if (ret == 0)
        ret = krb5_storage_fsync(sp);
    krb5_storage_free(sp);
    close(fd);
    return ret;
}

static krb5_error_code KRB5_CALLCONV
fkt_remove_entry(krb5_context context,
		 krb5_keytab id,
		 krb5_keytab_entry *entry)
{
    krb5_ssize_t bytes;
    krb5_keytab_entry e;
    krb5_kt_cursor cursor;
    off_t pos_start, pos_end;
    int found = 0;
    krb5_error_code ret;

    ret = fkt_start_seq_get_int(context, id, O_RDWR | O_BINARY | O_CLOEXEC, 1, &cursor);
    if(ret != 0)
	goto out; /* return other error here? */
    while(fkt_next_entry_int(context, id, &e, &cursor,
			     &pos_start, &pos_end) == 0) {
	if(krb5_kt_compare(context, &e, entry->principal,
			   entry->vno, entry->keyblock.keytype)) {
	    int32_t len;
	    unsigned char buf[128];
	    found = 1;
	    krb5_storage_seek(cursor.sp, pos_start, SEEK_SET);
	    len = pos_end - pos_start - 4;
	    ret = krb5_store_int32(cursor.sp, -len);
            if (ret)
                goto out;
	    memset(buf, 0, sizeof(buf));
	    while(len > 0) {
		bytes = krb5_storage_write(cursor.sp, buf,
		    min((size_t)len, sizeof(buf)));
                if (bytes != min((size_t)len, sizeof(buf))) {
                    ret = bytes == -1 ? errno : KRB5_KT_END;
                    goto out;
                }
		len -= min((size_t)len, sizeof(buf));
	    }
	}
	krb5_kt_free_entry(context, &e);
    }
    krb5_kt_end_seq_get(context, id, &cursor);
  out:
    if (!found) {
	krb5_clear_error_message (context);
	return KRB5_KT_NOTFOUND;
    }
    return ret;
}

const krb5_kt_ops krb5_fkt_ops = {
    "FILE",
    fkt_resolve,
    fkt_get_name,
    fkt_close,
    fkt_destroy,
    NULL, /* get */
    fkt_start_seq_get,
    fkt_next_entry,
    fkt_end_seq_get,
    fkt_add_entry,
    fkt_remove_entry,
    NULL,
    0
};

const krb5_kt_ops krb5_wrfkt_ops = {
    "WRFILE",
    fkt_resolve,
    fkt_get_name,
    fkt_close,
    fkt_destroy,
    NULL, /* get */
    fkt_start_seq_get,
    fkt_next_entry,
    fkt_end_seq_get,
    fkt_add_entry,
    fkt_remove_entry,
    NULL,
    0
};

const krb5_kt_ops krb5_javakt_ops = {
    "JAVA14",
    fkt_resolve_java14,
    fkt_get_name,
    fkt_close,
    fkt_destroy,
    NULL, /* get */
    fkt_start_seq_get,
    fkt_next_entry,
    fkt_end_seq_get,
    fkt_add_entry,
    fkt_remove_entry,
    NULL,
    0
};
