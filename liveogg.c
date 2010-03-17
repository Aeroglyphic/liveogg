/********************************************************************
*																	*
* THIS FILE IS PART OF THE OggVorbis SOFTWARE CODEC SOURCE CODE.	*
* USE, DISTRIBUTION AND REPRODUCTION OF THIS LIBRARY SOURCE IS		*
* GOVERNED BY A BSD-STYLE SOURCE LICENSE INCLUDED WITH THIS SOURCE	*
* IN 'COPYING'. PLEASE READ THESE TERMS BEFORE DISTRIBUTING.		*
*																	*
* THE OggVorbis SOURCE CODE IS (C) COPYRIGHT 1994-2001				*
* by the XIPHOPHORUS Company http://www.xiph.org/					*

******************************************************************** 

function: simple example encoder last mod: $Id: encoder_example.c,v 1.21 2001/02/26 03:50:38 xiphmont Exp $ 

********************************************************************/

/* takes a stereo 16bit 44.1kHz WAV file from stdin and encodes it into a Vorbis bitstream */

/* Note that this is POSIX, not ANSI, code */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>
#include <vorbis/vorbisenc.h>

#include <sys/ioctl.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/soundcard.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>



/* blocking/non blocking flags */

#define S_DELAY 0
#define S_NDELAY 1

#define S_RESET 0
#define S_SET 1
#define S_NAMLEN 256


/* socket data structure */ typedef struct
{
  struct sockaddr_in sin;
  int sinlen;
  int bindflag;
  int sd;
}
sckt;



/* finally all the functions we support */

int sserver (sckt * sp, int port, int sync);
int sclient (sckt * sp, char *name, int port);
sckt *sopen (void);
int sclose (sckt * sp);


extern int errno;



#define READ 1024
signed char readbuffer[READ * 4 + 44];	/* out of the data segment, not the stack */




/* Create a new socket which can be bound to a socket later */
sckt * sopen (void)
{
  sckt *sp;
  if ((sp = (sckt *) malloc (sizeof (sckt))) == 0)
    return 0;
  if ((sp->sd = socket (AF_INET, SOCK_STREAM, 0)) == -1)
    {
      free (sp);
      return 0;
    }
  sp->sinlen = sizeof (sp->sin);
  sp->bindflag = S_RESET;
  return sp;
}

/* close an existing socket */
int sclose (sckt * sp)
{
  int sd;
  sd = sp->sd;
  free (sp);
  return close (sd);
}

/* Attempt to connect to a listening socket */
int sclient (sckt * sp, char *name, int port)
{
  struct hostent *hostent;
  if ((hostent = gethostbyname (name)) == 0)
    return -1;
  sp->sin.sin_family = (short) hostent->h_addrtype;
  sp->sin.sin_port = htons ((unsigned short) port);
  sp->sin.sin_addr.s_addr = *(unsigned long *) hostent->h_addr;
  if (connect (sp->sd, (struct sockaddr *) &sp->sin, sp->sinlen) == -1)
    return -1;
  return sp->sd;
}


/* Open socket for listening - return on connect */
int sserver (sckt * sp, int port, int sync)
{
  int flags;
  struct hostent *hostent;
  char localhost[S_NAMLEN + 1];
  if (sp->bindflag == S_RESET)
    {
      if (gethostname (localhost, S_NAMLEN) == -1
	  || (hostent = gethostbyname (localhost)) == 0)
	return -1;
      sp->sin.sin_family = (short) hostent->h_addrtype;
      sp->sin.sin_port = htons ((unsigned short) port);
      sp->sin.sin_addr.s_addr = 0;	/* sp->sin.sin_addr.s_addr = *(unsigned long *)hostent->h_addr; */
      if (bind (sp->sd, (struct sockaddr *) &sp->sin, sp->sinlen) == -1
	  || listen (sp->sd, 5) == -1)
	return -1;
      sp->bindflag = S_SET;
    }
  switch (sync)
    {
    case S_DELAY:
      if ((flags = fcntl (sp->sd, F_GETFL)) == -1
	  || fcntl (sp->sd, F_SETFL, flags & O_NDELAY) == -1)
	return -1;
      break;
    case S_NDELAY:
      if ((flags = fcntl (sp->sd, F_GETFL)) == -1
	  || fcntl (sp->sd, F_SETFL, flags | O_NDELAY) == -1)
	return -1;
      break;
    default:
      return -1;
    }
  return accept (sp->sd, (struct sockaddr *) &sp->sin, &sp->sinlen);
}


int source (int requested, short *out)
{
  static int time = 0;
  static short value = 10000;
  int sample;
  sample = 0;
  for (sample = 0; sample < requested; sample++)
    {
      out[sample] = (100 * sample) % 30000;
    }
  return requested;
}

int open_sound_card (char *device, int sample_rate, int channels)
{
  int fd;
  int format, stereo, rate;
  rate = sample_rate;
  if (channels > 1)
    {
      stereo = 1;
    }
  else
    {
      stereo = 0;
    }

  if ((fd = open (device, O_RDONLY, 0)) == -1)
    {
      perror ("/dev/dsp");
      fprintf (stderr, "Bugger - we can't open the sound device %s\n",
	       device); return -1;
    }
  format = AFMT_S16_LE;
  if (ioctl (fd, SNDCTL_DSP_SETFMT, &format) == -1)
    {
      perror ("SNDCTL_DSP_SETFMT");
      fprintf (stderr, "Bugger -we can't set the recording format\n");
      close (fd);
      return -1;
    }

  if (format != AFMT_S16_LE)
    {
      fprintf (stderr, "format not set correctly\n");
      close (fd);
      return -1;
    }
  if (ioctl (fd, SNDCTL_DSP_STEREO, &stereo) == -1)
    {
      perror ("SNDCTL_DSP_STEREO");
      fprintf (stderr, "channels not set correctly\n");
      close (fd);
      return -1;
    }
  if (ioctl (fd, SNDCTL_DSP_SPEED, &rate) == -1)
    {
      perror ("SNDCTL_DSP_SPEED");
      fprintf (stderr, "speed not set correctly\n");
      close (fd);
      return -1;
    }

  return fd;
}

/* actually - I don't know what is left or right..... */
int record_and_split (int fd, int num, short *left, short *right)
{
  int i, j, act, max;
  static int last_size = 0;
  static short *rdbuf = NULL;

  if (last_size != num)
    {
      rdbuf = realloc (rdbuf, num * 2 * sizeof (short));
      last_size = num;
    }

  act = read (fd, rdbuf, num * 2 * sizeof (short));
  if (act % (2 * sizeof (short)))
    {
      fprintf (stderr, "read an odd number of bytes.... %d \n", act);
    }
  j = act / (2 * sizeof (short));
  max = 0;
  for (i = 0; i < j; i++)
    {
      left[i] = rdbuf[i * 2];
      right[i] = rdbuf[i * 2 + 1];
      if (abs (left[i]) > max)
	{
	  max = abs (left[i]);
	}
      if (abs (right[i]) > max)
	{
	  max = abs (right[i]);
	}
    }
  fprintf (stderr, "\r%6d \r", max);
  return j;
}				/* SOURCE $mountpoint ICE/1.0\n ice-password: $password\n ice-name: $name\n ice-url: $url\n ice-genre: $genre\n ice-bitrate: $bitrate\n ice-public: $public\n ice-description: $description\n\n */


int open_connection_to_server (char *server, int port, char *mountpoint,
			   char *password, int br, char *name, char *genre,
			   char *url, char *description, int pub, sckt * sp)
{
  int sd;
  char buffer[4096];
  if ((sp = sopen ()) == 0)
    {
      fprintf (stderr, "couldn't create socket\n");
      return -1;
    }
  if ((sd = sclient (sp, server, port)) == -1)
    {
      fprintf (stderr, "couldn't connect to server\n");
      sclose (sp);
      return -1;
    }				/* now try to login */
  sprintf (buffer,
	   "SOURCE %s ICE/1.0\nice-password: %s\nice-name: %s\nice-url: %s\nice-genre: %s\nice-bitrate: %d\nice-public: %d\nice-description: %s\n\n",
	   mountpoint, password, name, url, genre, br, pub, description);
  write (sd, buffer, strlen (buffer));
  return sd;
}


int main (int argc, char *argv[])
{
  ogg_stream_state os;		/* take physical pages, weld into a logical stream of packets */
  ogg_page og;			/* one Ogg bitstream page. Vorbis packets are inside */
  ogg_packet op;		/* one raw packet of data for decode */
  vorbis_info vi;		/* struct that stores all the static vorbis bitstream settings */
  vorbis_comment vc;		/* struct that stores all the user comments */

  vorbis_dsp_state vd;		/* central working state for the packet->PCM decoder */
  vorbis_block vb;		/* local working space for packet->PCM decode */

  int eos = 0;


/* liveice variables */ int audio_fd;
  int socket_fd;
  sckt *sp;
  char *server;
  char *password;
  char *name;
  char *genre;
  char *url;
  char *mountpoint;
  char *description;
  int port, samplerate, channels, bitrate, public;
  float max;
  if (argc < 13)
    {
      fprintf (stderr,
	       "Usage:\n%s <server> <port> <mountpoint> <password> <name> <genre> <url> <description> <public> <samplerate(Hz)> <channels> <bitrate(bits/sec)>\n",
	       argv[0]);
      exit (1);
    }
  server = argv[1];
  port = atoi (argv[2]);
  mountpoint = argv[3];
  password = argv[4];
  name = argv[5];
  genre = argv[6];
  url = argv[7];
  description = argv[8];
  public = atoi (argv[9]);	/* ignore this */
  samplerate = atoi (argv[10]);
  channels = atoi (argv[11]);
  bitrate = atoi (argv[12]) / 1000;

  audio_fd = open_sound_card ("/dev/dsp", 44100, 2);
  socket_fd =
    open_connection_to_server (server, port, mountpoint, password,
			       bitrate, name, genre, url, description, public, sp);	/* rip out .wav header *//* fread(readbuffer,1,44,stdin); */

/********** Encode setup ************/

/* choose an encoding mode *//* (mode 0: 44kHz stereo uncoupled, roughly 128kbps VBR) */
  vorbis_info_init (&vi);
  vorbis_encode_init (&vi, 2, 44100, -1, 128000, -1);

/* add a comment */
vorbis_comment_init (&vc);
vorbis_comment_add (&vc, "ARTIST=.oO( Radio FG )Oo.");
vorbis_comment_add (&vc, "TITLE=live techno radio from France");
vorbis_comment_add (&vc, "DATE=2001-08-22");
vorbis_comment_add (&vc, "GENRE=techno house live");
vorbis_comment_add (&vc, "COMMENT=liveogg encoder 0.1.2");

/* set up the analysis state and auxiliary encoding storage */
  vorbis_analysis_init (&vd, &vi);
  vorbis_block_init (&vd, &vb);	/* set up our packet->stream encoder *//* pick a random serial number; that way we can more likely build chained streams just by concatenation */
  srand (time (NULL));
  ogg_stream_init (&os, rand ());

/* Vorbis streams begin with three headers; the initial header (with most of the codec setup parameters) which is mandated by the Ogg bitstream spec. The second header holds any comment fields. The third header holds the bitstream codebook. We merely need to make the headers, then pass them to libvorbis one at a time; libvorbis handles the additional Ogg bitstream constraints */

  {
    ogg_packet header;
    ogg_packet header_comm;
    ogg_packet header_code;

    vorbis_analysis_headerout (&vd, &vc, &header, &header_comm, &header_code);
    ogg_stream_packetin (&os, &header);	/* automatically placed in its own page */
    ogg_stream_packetin (&os, &header_comm);
    ogg_stream_packetin (&os, &header_code);

/* We don't have to write out here, but doing so makes streaming * much easier, so we do, flushing ALL pages. This ensures the actual * audio data will start on a new page */
    while (!eos)
      {
	int result = ogg_stream_flush (&os, &og);
	if (result == 0)
	  break;
	write (socket_fd, og.header, og.header_len);
	write (socket_fd, og.body, og.body_len);
      }

  }
  while (!eos)
    {
      long i;
      long bytes = read (audio_fd, readbuffer, READ * 4);	/* stereo hardwired here */

      if (bytes == 0)
	{			/* end of file. this can be done implicitly in the mainline, but it's easier to see here in non-clever fashion. Tell the library we're at end of stream so that it can handle the last frame and mark end of stream in the output properly */
	  vorbis_analysis_wrote (&vd, 0);
	}
      else
	{			/* data to encode *//* expose the buffer to submit data */
	  float **buffer = vorbis_analysis_buffer (&vd, READ);
	  max = 0;		/* uninterleave samples */
	  for (i = 0; i < bytes / 4; i++)
	    {
	      buffer[0][i] =
		((readbuffer[i * 4 + 1] << 8) |
		 (0x00ff & (int) readbuffer[i * 4])) / 32768.f;
	      if (fabs (buffer[0][i]) > max)
		{
		  max = fabs (buffer[0][i]);
		}
	      buffer[1][i] =
		((readbuffer[i * 4 + 3] << 8) |
		 (0x00ff & (int) readbuffer[i * 4 + 2])) / 32768.f;
	      if (fabs (buffer[1][i]) > max)
		{
		  max = fabs (buffer[1][i]);
		}
	    }
	  fprintf (stderr, "\r%6f ", max);	/* tell the library how much we actually submitted */
	  vorbis_analysis_wrote (&vd, i);
	}			/* vorbis does some data preanalysis, then divvies up blocks for more involved (potentially parallel) processing. Get a single block for encoding now */
      while (vorbis_analysis_blockout (&vd, &vb) == 1)
	{

	  /* analysis */ vorbis_analysis (&vb, &op);
	  /* weld the packet into the bitstream */
	  ogg_stream_packetin (&os, &op);

/* write out pages (if any) */ while (!eos)
	    {
	      int result = ogg_stream_pageout (&os, &og);
	      if (result == 0)
		break;
	      write (socket_fd, og.header, og.header_len);
	      write (socket_fd, og.body, og.body_len);

/* this could be set above, but for illustrative purposes, I do it here (to show that vorbis does know where the stream ends) */
	      if (ogg_page_eos (&og))
		eos = 1;

	    }
	}
    }

/* clean up and exit. vorbis_info_clear() must be called last */
  ogg_stream_clear (&os);
  vorbis_block_clear (&vb);
  vorbis_dsp_clear (&vd);
  vorbis_comment_clear (&vc);
  vorbis_info_clear (&vi);	/* ogg_page and ogg_packet structs always point to storage in libvorbis. They're never freed or manipulated directly */
  fprintf (stderr, "Done.\n");
  return (0);
}
