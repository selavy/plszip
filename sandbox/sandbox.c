#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <utime.h>
#include "zlib.h"

#define INFBACK 0

/* function declaration */
#define local static

/* buffer constants */
#define SIZE 32768U         /* input and output buffer sizes */
#define PIECE 16384         /* limits i/o chunks for 16-bit int case */

/* structure for infback() to pass to input function in() -- it maintains the
   input file and a buffer of size SIZE */
struct ind {
    int infile;
    unsigned char *inbuf;
};

/* Load input buffer, assumed to be empty, and return bytes loaded and a
   pointer to them.  read() is called until the buffer is full, or until it
   returns end-of-file or error.  Return 0 on error. */
local unsigned in(void *in_desc, z_const unsigned char **buf)
{
    int ret;
    unsigned len;
    unsigned char *next;
    struct ind *me = (struct ind *)in_desc;

    next = me->inbuf;
    *buf = next;
    len = 0;
    do {
        ret = PIECE;
        if ((unsigned)ret > SIZE - len)
            ret = (int)(SIZE - len);
        ret = (int)read(me->infile, next, ret);
        if (ret == -1) {
            len = 0;
            break;
        }
        next += ret;
        len += ret;
    } while (ret != 0 && len < SIZE);
    return len;
}

/* structure for infback() to pass to output function out() -- it maintains the
   output file, a running CRC-32 check on the output and the total number of
   bytes output, both for checking against the gzip trailer.  (The length in
   the gzip trailer is stored modulo 2^32, so it's ok if a long is 32 bits and
   the output is greater than 4 GB.) */
struct outd {
    int outfile;
    int check;                  /* true if checking crc and total */
    unsigned long crc;
    unsigned long total;
};

/* Write output buffer and update the CRC-32 and total bytes written.  write()
   is called until all of the output is written or an error is encountered.
   On success out() returns 0.  For a write failure, out() returns 1.  If the
   output file descriptor is -1, then nothing is written.
 */
local int out(void *out_desc, unsigned char *buf, unsigned len)
{
    int ret;
    struct outd *me = (struct outd *)out_desc;

    if (me->check) {
        me->crc = crc32(me->crc, buf, len);
        me->total += len;
    }
    if (me->outfile != -1)
        do {
            ret = PIECE;
            if ((unsigned)ret > len)
                ret = (int)len;
            ret = (int)write(me->outfile, buf, ret);
            if (ret == -1)
                return 1;
            buf += ret;
            len -= ret;
        } while (len != 0);
    return 0;
}

/* next input byte macro for use inside lunpipe() and gunpipe() */
#define NEXT() (have ? 0 : (have = in(indp, &next)), \
                last = have ? (have--, (int)(*next++)) : -1)

/* memory for gunpipe() */
unsigned char inbuf[SIZE];              /* input buffer */
unsigned char outbuf[SIZE];             /* output buffer */
unsigned char slidingwindow[SIZE];      /* gzip 32K sliding window */

/* Decompress a gzip file from infile to outfile.  strm is assumed to have been
   successfully initialized with inflateBackInit().  The input file may consist
   of a series of gzip streams, in which case all of them will be decompressed
   to the output file.  If outfile is -1, then the gzip stream(s) integrity is
   checked and nothing is written.

   The return value is a zlib error code: Z_MEM_ERROR if out of memory,
   Z_DATA_ERROR if the header or the compressed data is invalid, or if the
   trailer CRC-32 check or length doesn't match, Z_BUF_ERROR if the input ends
   prematurely or a write error occurs, or Z_ERRNO if junk (not a another gzip
   stream) follows a valid gzip stream.
 */
local int gunpipe(z_stream *strm, int infile, int outfile)
{
    int ret, first, last;
    unsigned have, flags, len;
    z_const unsigned char *next = NULL;
    struct ind ind, *indp;
    struct outd outd;

    /* setup input buffer */
    ind.infile = infile;
    ind.inbuf = inbuf;
    indp = &ind;

    /* decompress concatenated gzip streams */
    have = 0;                               /* no input data read in yet */
    strm->next_in = Z_NULL;                 /* so Z_BUF_ERROR means EOF */

    if (NEXT() == -1) {
        ret = Z_OK;
        return ret;                          /* empty gzip stream is ok */
    }

    /* look for the two magic header bytes for a gzip stream */
    if (!(last == 31 && NEXT() == 139)) {
        strm->msg = (char *)"incorrect header check";
        ret = Z_DATA_ERROR;
        return ret;                          /* not a gzip or compress header */
    }

    /* process remainder of gzip header */
    ret = Z_BUF_ERROR;
    if (NEXT() != 8) {                  /* only deflate method allowed */
        if (last == -1) return ret;
        strm->msg = (char *)"unknown compression method";
        ret = Z_DATA_ERROR;
        return ret;
    }
    flags = NEXT();                     /* header flags */
    NEXT();                             /* discard mod time, xflgs, os */
    NEXT();
    NEXT();
    NEXT();
    NEXT();
    NEXT();
    if (last == -1) return ret;
    if (flags & 0xe0) {
        strm->msg = (char *)"unknown header flags set";
        ret = Z_DATA_ERROR;
        return ret;
    }
    if (flags & 4) {                    /* extra field */
        len = NEXT();
        len += (unsigned)(NEXT()) << 8;
        if (last == -1) return ret;
        while (len > have) {
            len -= have;
            have = 0;
            if (NEXT() == -1) break;
            len--;
        }
        if (last == -1) return ret;
        have -= len;
        next += len;
    }
    if (flags & 8)                      /* file name */
        while (NEXT() != 0 && last != -1)
            ;
    if (flags & 16)                     /* comment */
        while (NEXT() != 0 && last != -1)
            ;
    if (flags & 2) {                    /* header crc */
        NEXT();
        NEXT();
    }
    if (last == -1) return ret;

    /* set up output */
    outd.outfile = outfile;
    outd.check = 1;
    outd.crc = crc32(0L, Z_NULL, 0);
    outd.total = 0;

    /* decompress data to output */

    // printf("have = %u\n", have);
    for (int i = 0; i < 4; ++i) {
        printf("%02x %02x %02x %02x\n",
                next[i+0], next[i+1], next[i+2], next[i+3]);
    }

    strm->next_in = next;
    strm->avail_in = have;
#if INFBACK
    ret = inflateBack(strm, in, indp, out, &outd);
#else
    // TEMP TEMP: only works on small buffers, would normally
    // need to call this in a loop, but just for testing only
    // calling once.
    ret = inflate(strm, Z_NO_FLUSH);
    if (ret == Z_STREAM_END) {
        size_t len = SIZE - strm->avail_out;
        printf("Writing data: %zu\n", len);
        do {
            unsigned char *buf = outbuf;
            ret = PIECE;
            if ((unsigned)ret > len)
                ret = (int)len;
            ret = (int)write(outd.outfile, buf, ret);
            if (ret == -1)
                return 1;
            buf += ret;
            len -= ret;
        } while (len != 0);
        ret = Z_STREAM_END;
    }
#endif
    if (ret != Z_STREAM_END) return ret;
    next = strm->next_in;
    have = strm->avail_in;
    strm->next_in = Z_NULL;             /* so Z_BUF_ERROR means EOF */

    /* check trailer */
    ret = Z_BUF_ERROR;
    if (NEXT() != (int)(outd.crc & 0xff) ||
            NEXT() != (int)((outd.crc >> 8) & 0xff) ||
            NEXT() != (int)((outd.crc >> 16) & 0xff) ||
            NEXT() != (int)((outd.crc >> 24) & 0xff)) {
        /* crc error */
        if (last != -1) {
            strm->msg = (char *)"incorrect data check";
            ret = Z_DATA_ERROR;
        }
        return ret;
    }
    if (NEXT() != (int)(outd.total & 0xff) ||
            NEXT() != (int)((outd.total >> 8) & 0xff) ||
            NEXT() != (int)((outd.total >> 16) & 0xff) ||
            NEXT() != (int)((outd.total >> 24) & 0xff)) {
        /* length error */
        if (last != -1) {
            strm->msg = (char *)"incorrect length check";
            ret = Z_DATA_ERROR;
        }
        return ret;
    }

    if (NEXT() == -1) {
        ret = Z_OK;
    }

    /* clean up and return */
    return ret;
}

/* Copy file attributes, from -> to, as best we can.  This is best effort, so
   no errors are reported.  The mode bits, including suid, sgid, and the sticky
   bit are copied (if allowed), the owner's user id and group id are copied
   (again if allowed), and the access and modify times are copied. */
local void copymeta(char *from, char *to)
{
    struct stat was;
    struct utimbuf when;

    /* get all of from's Unix meta data, return if not a regular file */
    if (stat(from, &was) != 0 || (was.st_mode & S_IFMT) != S_IFREG)
        return;

    /* set to's mode bits, ignore errors */
    (void)chmod(to, was.st_mode & 07777);

    /* copy owner's user and group, ignore errors */
    (void)chown(to, was.st_uid, was.st_gid);

    /* copy access and modify times, ignore errors */
    when.actime = was.st_atime;
    when.modtime = was.st_mtime;
    (void)utime(to, &when);
}

/* Decompress the file inname to the file outnname, of if test is true, just
   decompress without writing and check the gzip trailer for integrity.  If
   inname is NULL or an empty string, read from stdin.  If outname is NULL or
   an empty string, write to stdout.  strm is a pre-initialized inflateBack
   structure.  When appropriate, copy the file attributes from inname to
   outname.

   gunzip() returns 1 if there is an out-of-memory error or an unexpected
   return code from gunpipe().  Otherwise it returns 0.
 */
local int gunzip(z_stream *strm, char *inname, char *outname, int test)
{
    int ret;
    int infile, outfile;

    /* open files */
    if (inname == NULL || *inname == 0) {
        inname = "-";
        infile = 0;     /* stdin */
    }
    else {
        infile = open(inname, O_RDONLY, 0);
        if (infile == -1) {
            fprintf(stderr, "gun cannot open %s\n", inname);
            return 0;
        }
    }
    if (test)
        outfile = -1;
    else if (outname == NULL || *outname == 0) {
        outname = "-";
        outfile = 1;    /* stdout */
    }
    else {
        outfile = open(outname, O_CREAT | O_TRUNC | O_WRONLY, 0666);
        if (outfile == -1) {
            close(infile);
            fprintf(stderr, "gun cannot create %s\n", outname);
            return 0;
        }
    }
    errno = 0;

    /* decompress */
    ret = gunpipe(strm, infile, outfile);
    if (outfile > 2) close(outfile);
    if (infile > 2) close(infile);

    /* interpret result */
    switch (ret) {
    case Z_OK:
    case Z_ERRNO:
        if (infile > 2 && outfile > 2) {
            copymeta(inname, outname);          /* copy attributes */
            unlink(inname);
        }
        if (ret == Z_ERRNO)
            fprintf(stderr, "gun warning: trailing garbage ignored in %s\n",
                    inname);
        break;
    case Z_DATA_ERROR:
        if (outfile > 2) unlink(outname);
        fprintf(stderr, "gun data error on %s: %s\n", inname, strm->msg);
        break;
    case Z_MEM_ERROR:
        if (outfile > 2) unlink(outname);
        fprintf(stderr, "gun out of memory error--aborting\n");
        return 1;
    case Z_BUF_ERROR:
        if (outfile > 2) unlink(outname);
        if (strm->next_in != Z_NULL) {
            fprintf(stderr, "gun write error on %s: %s\n",
                    outname, strerror(errno));
        }
        else if (errno) {
            fprintf(stderr, "gun read error on %s: %s\n",
                    inname, strerror(errno));
        }
        else {
            fprintf(stderr, "gun unexpected end of file on %s\n",
                    inname);
        }
        break;
    default:
        if (outfile > 2) unlink(outname);
        fprintf(stderr, "gun internal error--aborting\n");
        return 1;
    }
    return 0;
}

/* Process the gun command line arguments.  See the command syntax near the
   beginning of this source file. */
int main(int argc, char **argv)
{
    int ret, len, test;
    char *outname;
    unsigned char *window;
    z_stream strm;

    /* initialize inflateBack state for repeated use */
    window = slidingwindow;
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
#if INFBACK
    ret = inflateBackInit(&strm, 15, window);
#else
    // ret = inflateInit(&strm);
    ret = inflateInit2(&strm, 15);
#endif
    if (ret != Z_OK) {
        fprintf(stderr, "gun out of memory error--aborting\n");
        return 1;
    }

    /* decompress each file to the same name with the suffix removed */
    argc--;
    argv++;
    test = 0;
    if (argc && strcmp(*argv, "-h") == 0) {
        fprintf(stderr, "gun 1.6 (17 Jan 2010)\n");
        fprintf(stderr, "Copyright (C) 2003-2010 Mark Adler\n");
        fprintf(stderr, "usage: gun [-t] [file1.gz [file2.Z ...]]\n");
        return 0;
    }
    if (argc && strcmp(*argv, "-t") == 0) {
        test = 1;
        argc--;
        argv++;
    }
    if (argc)
        do {
            if (test)
                outname = NULL;
            else {
                len = (int)strlen(*argv);
                if (strcmp(*argv + len - 3, ".gz") == 0 ||
                    strcmp(*argv + len - 3, "-gz") == 0)
                    len -= 3;
                else if (strcmp(*argv + len - 2, ".z") == 0 ||
                    strcmp(*argv + len - 2, "-z") == 0 ||
                    strcmp(*argv + len - 2, "_z") == 0 ||
                    strcmp(*argv + len - 2, ".Z") == 0)
                    len -= 2;
                else {
                    fprintf(stderr, "gun error: no gz type on %s--skipping\n",
                            *argv);
                    continue;
                }
                outname = malloc(len + 1);
                if (outname == NULL) {
                    fprintf(stderr, "gun out of memory error--aborting\n");
                    ret = 1;
                    break;
                }
                memcpy(outname, *argv, len);
                outname[len] = 0;
            }
            ret = gunzip(&strm, *argv, outname, test);
            if (outname != NULL) free(outname);
            if (ret) break;
        } while (argv++, --argc);
    else
        ret = gunzip(&strm, NULL, NULL, test);

    /* clean up */
#if INFBACK
    inflateBackEnd(&strm);
#else
    inflateEnd(&strm);
#endif
    return ret;
}
