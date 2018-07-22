# jpegreader

The jpegreader allows streaming JPEG-frames from a V4L2-device using MJPEG.
The advantage of this method over alternatives (e.g. FFMpeg -f mpjpeg) is 
that neither MJPEG nor JFIF nor JPEG contains any header with the byte length
of the JPEG data. This means that a consumer always needs to compare every
single byte to look for the \xff\xd8 marker that marks the start of an
image packet. With jpegreader, you can skip over the image packet in order
to separate the JPEG frames without any significant CPU usage.

Example usage:

    jpegreader -h -s 1280x720 -i /dev/video1 | use_jpeg_frames

The standard output contains a continuous stream of packets like:

    \x01<frame-length>\nJPEG_FRAMEDATA\x01<frame-length>\nJPEG_FRAMEDATA...

Options:

    --header,-h           Embed \x01<frame-length>\n header. Default behavior
                          prints frame lengths in stderr after frame is flushed
                          to stdout.
    --resolution,-r,-s    Set custom resolution. Default is 1920x1080.
    --device,-d,-i        Set custom V4L2 input device. Default is /dev/video0.

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

Here we write four JPEG files using Bash and Perl ready to copy&paste in your shell.
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
    jpegreader --header | perl "$tmp"
    rm -f "$tmp"

# NodeJS example

Here we write four JPEG files using NodeJS ready to copy&paste in your shell.
Note that jpegreader uses /dev/video0 by default.

    node <<'EOF'
    var readframe = (function()
    {
        var fs = require('fs');
        var fi = 0;
        var fn = 4;
        var chr;
        var bc;
        var frame;
        var mode = 0;

        return function()
        {
            while(true)
            {
                if(mode === 0)
                {
                    while(true)
                    {
                        chr = reader.stdout.read(1);
                        if(chr === null)
                        {
                            return;
                        }
                        else if(chr === '\x01')
                        {
                            bc = '';
                            mode = 1;
                            break;
                        }
                    }
                }
                if(mode === 1)
                {
                    while(true)
                    {
                        chr = reader.stdout.read(1);
                        if(chr === null)
                        {
                            return;
                        }
                        else if(chr === '\n')
                        {
                            bc = parseInt(bc);
                            if(bc)
                            {
                                mode = 2;
                                break;
                            }
                            mode = 0;
                            break;
                        }

                        bc += chr;
                    }
                }
                if(mode === 2)
                {
                    frame = reader.stdout.read(bc);
                    if(frame === null)
                    {
                        return;
                    }
                    else
                    {
                        fs.writeFileSync('frame.' + fi + '.jpg', frame, {encoding: 'binary'});
                        console.log('frame.' + fi + '.jpg: ' + bc + ' bytes written');
                        if(++fi >= fn)
                        {
                            reader.kill('SIGINT');
                            reader.stdout.destroy();
                            return;
                        }
                        mode = 0;
                        break;
                    }
                }
            }
        };
    })();
    
    var reader = require('child_process').spawn('jpegreader', ['--header']);
    reader.stdout.setEncoding('binary');
    reader.stdout.on('readable', readframe);
    reader.on('exit', function(code)
    {
        console.log('jpegreader exited with code: ' + code);
    });
    EOF

If we would like to grab frame by frame, we could use the following code instead. This version uses the stderr output from jpegreader to grab frames. This is the recommended method, use this whenever you have access to two separate streams.

    var frameno = 0;
    var queue = [];
    var tryreadframe = function()
    {
        while(queue.length)
        {
            var frame = reader.stdout.read(queue[0]);
            if(frame)
            {
                queue.shift();

                if(frame[0] == '\xff' && frame[1] == '\xd8')
                {
                    console.log('jpeg frame read: ' + frame.length);
                    require('fs').writeFileSync('frame.' + frameno + '.jpg', frame, {encoding: 'binary'});
                    console.log('jpeg file written: frame.' + frameno + '.jpg');
                    ++frameno;
                }
                else
                {
                    console.log('corrupted data read: ' + frame.length);
                }
            }
            else
            {
                console.log('frame missed, ' + queue[0] + ' not available in stdout');
                return;
            }
        }
    };

    var reader = require('child_process').spawn('jpegreader');
    reader.stdout.setEncoding('binary');
    reader.stdout.on('readable', tryreadframe);
    reader.stderr.on('data', function(buf)
    {
        var length = parseInt(buf+'');
        if(length)
        {
            queue.push(length);
            tryreadframe();
        }
    });
    reader.on('exit', function(code)
    {
        console.log('jpegreader exited with code: ' + code);
        process.exit(1);
    });

    reader.kill('SIGCHLD');
    console.log('jpegreader is currently paused, use commands: next, play, pause');
    process.stdin.on('data', function(buf)
    {
        if(buf == 'play\n')
        {
            reader.kill('SIGCONT');
        }
        else if(buf == 'pause\n')
        {
            reader.kill('SIGCHLD');
        }
        else if(buf == 'next\n')
        {
            reader.kill('SIGUSR1');
        }
    });

