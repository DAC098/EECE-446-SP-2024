use std::net::TcpStream;
use std::io::{Read, Write};
use std::default::Default;

use rand::RngCore;
use rand::distributions::{Alphanumeric, DistString};
use anyhow::{Context, Result};
use clap::{Subcommand, Args};

#[derive(Debug, Args)]
pub struct BytesArgs {
    /// repeat the message or repeat the message n times
    #[arg(long)]
    repeat: Option<Option<usize>>,

    /// delay a certain amount of milliseconds between messages
    #[arg(long)]
    delay: Option<u64>,

    /// size of the read/write buffers
    #[arg(long, default_value="2048")]
    buf_size: usize,

    #[command(subcommand)]
    command: Option<MessageCmds>
}

impl Default for BytesArgs {
    fn default() -> Self {
        BytesArgs {
            repeat: None,
            buf_size: 2048,
            delay: None,
            command: Default::default(),
        }
    }
}

#[derive(Debug, Subcommand)]
enum MessageCmds {
    /// sends a "ping" message
    Ping,

    /// sends a set of random bytes
    Rand {
        #[arg(short, long, default_value="64")]
        len: usize
    },

    /// sends a set of random alphanumeric bytes
    AlphaRand {
        /// amount of bytes to send
        #[arg(short, long, default_value="64")]
        len: usize
    },

    /// sends a predefined string message
    Message {
        /// the message to send
        #[arg(short, long)]
        say: String
    }
}

impl MessageCmds {
    fn get_message(&self) -> Result<Vec<u8>> {
        let rtn = match self {
            MessageCmds::Ping => {
                println!("creating ping message");

                Vec::from(b"ping")
            },
            MessageCmds::Rand { len } => {
                println!("creating rand bytes {}", len);

                let mut bytes = vec![0u8; *len];

                rand::thread_rng()
                    .try_fill_bytes(&mut bytes)
                    .context("failed to generate random bytes")?;

                bytes
            },
            MessageCmds::AlphaRand { len } => {
                println!("creating rand alphanumeric bytes {}", len);

                let rand_string = Alphanumeric.sample_string(&mut rand::thread_rng(), *len);

                rand_string.into_bytes()
            },
            MessageCmds::Message { say } => {
                println!("creating message {}", say.len());

                say.clone().into_bytes()
            }
        };

        Ok(rtn)
    }
}

impl Default for MessageCmds {
    fn default() -> Self {
        MessageCmds::Ping
    }
}

pub fn handle(mut stream: TcpStream, mut args: BytesArgs) -> Result<()> {
    let command = args.command.take().unwrap_or_default();

    if let Some(maybe_n_times) = args.repeat {
        if let Some(n_times) = maybe_n_times {
            for _ in 0..n_times {
                let bytes = command.get_message()?;

                write_to_socket(&mut stream, &bytes, args.buf_size)?;

                read_from_socket(&mut stream, bytes.len(), args.buf_size)?;

                if let Some(ms) = &args.delay {
                    let duration = std::time::Duration::from_millis(*ms);

                    std::thread::sleep(duration);
                }
            }
        } else {
            loop {
                let bytes = command.get_message()?;

                write_to_socket(&mut stream, &bytes, args.buf_size)?;

                read_from_socket(&mut stream, bytes.len(), args.buf_size)?;

                if let Some(ms) = &args.delay {
                    let duration = std::time::Duration::from_millis(*ms);

                    std::thread::sleep(duration);
                }
            }
        }
    } else {
        let bytes = command.get_message()?;

        write_to_socket(&mut stream, &bytes, args.buf_size)?;

        read_from_socket(&mut stream, bytes.len(), args.buf_size)?;
    }

    Ok(())
}

fn write_to_socket(stream: &mut TcpStream, bytes: &[u8], chunk_size: usize) -> Result<()> {
    let mut total_wrote = 0usize;
    let mut start_index = 0usize;
    let mut end_index = if bytes.len() < chunk_size {
        bytes.len()
    } else {
        chunk_size
    };

    loop {
        println!("writing byte range {}..{}", start_index, end_index);

        let wrote = stream.write(&bytes[start_index..end_index])
            .context("failed to write data to tcp stream")?;

        total_wrote += wrote;

        if total_wrote >= bytes.len() {
            break;
        }

        start_index += chunk_size;
        end_index += chunk_size;

        if bytes.len() < end_index {
            end_index = bytes.len();
        }
    }

    Ok(())
}

fn read_from_socket(stream: &mut TcpStream, expected_read: usize, chunk_size: usize) -> Result<()> {
    let mut buffer = vec![0u8; chunk_size];
    let mut total_read = 0usize;

    loop {
        buffer.fill(0);

        let read = stream.read(&mut buffer)
            .context("failed to read data from tcp stream")?;

        if read == 0 {
            println!("read 0 bytes client disconnected");
            break;
        }

        total_read += read;

        print!("[{}]:", read);

        for b in &buffer[0..read] {
            print!(" {:02x}", b);
        }

        let cow = String::from_utf8_lossy(&buffer[0..read]);

        println!("\n\u{2514}\u{2500}\"{}\"", cow);

        if total_read >= expected_read {
            println!("read expected amount of bytes");
            break;
        }
    }

    Ok(())
}
