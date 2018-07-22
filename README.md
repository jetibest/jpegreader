# jpegreader

The jpegreader allows streaming JPEG-frames from a V4L2-device using MJPEG.
The advantage of this method over alternatives (e.g. FFMpeg -f mpjpeg) is 
that neither MJPEG nor JFIF nor JPEG contains any header with the byte length
of the JPEG data. This means that a consumer always needs to compare every
single byte to look for the \xff\xd8 marker that marks the start of an
image packet. With jpegreader, you can skip over the image packet in order
to separate the JPEG frames without any significant CPU usage.

Example usage:

    jpegreader -s 1920x1080 -i /dev/video0 | use_jpeg_frames

The standard output contains a continuous stream of packets like:

    \x01<frame-length>\nJPEG_FRAMEDATA\x01<frame-length>\nJPEG_FRAMEDATA...

To parse individual JPEG frames is very simple:
  - Find the SOH-marker (Start of Heading: \x01, ^A)
  - Read number N until LF-marker (Line Feed: \n, \x0a, ^J)
  - Read N bytes after skipping the LF-marker.
  - If the next byte is not a SOH-marker, maybe discard the frame.

# Installation

    git clone https://github.com/jetibest/jpegreader
    cd jpegreader
    ./install.sh

# Perl example

Here we read four JPEG files using Bash and Perl ready to copy&paste in your shell.
Note that jpegreader uses /dev/video0 by default.

    tmp="$(mktemp)"
    cat >"$tmp" <<'EOF'
    #!/usr/bin/perl
    $fn = 4;
    $fi = 0;
    while($char = getc)
    {
        if($char == "\x01")
        {
            $count = "";
            while($char = getc)
            {
                if($char == "\n")
                {
                    $result = read(STDIN, $buffer, $count);
                    if($result == $count)
                    {
                        open($fh, '>:raw', "frame.$fi.jpg") or die "write error";
                        print $fh $buffer;
                        close $fh;
                        print "frame.$fi.jpg: $result bytes written\n";
                    }
                    if(++$fi >= $fn)
                    {
                        exit;
                    }
                    break;
                }
                $count .= $char;
            }
        }
    }
    EOF
    jpegreader | perl "$tmp"
    rm -f "$tmp"

