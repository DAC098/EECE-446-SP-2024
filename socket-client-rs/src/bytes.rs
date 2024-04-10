use std::net::TcpStream;
use std::io::{Read, Write};
use std::default::Default;
use std::fmt::Write as _;

use rand::RngCore;
use rand::distributions::{Alphanumeric, DistString};
use anyhow::{Context, Result};
use clap::{Subcommand, Args};

#[derive(Debug, Args)]
pub struct BytesArgs {
    /// repeat the message n times
    #[arg(long)]
    repeat: Option<usize>,

    /// repeat indefinitly
    #[arg(long)]
    repeat_inf: bool,

    /// delay a certain amount of milliseconds between messages
    #[arg(long)]
    delay: Option<u64>,

    /// size of the read/write buffers
    #[arg(long, default_value="2048")]
    buf_size: usize,

    /// will only send writes to the remote host
    #[arg(long)]
    no_read: bool,

    #[command(subcommand)]
    command: Option<MessageCmds>
}

impl Default for BytesArgs {
    fn default() -> Self {
        BytesArgs {
            repeat: None,
            repeat_inf: false,
            buf_size: 2048,
            delay: None,
            no_read: false,
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
    /// generates the specified message
    fn get_message(&self) -> Result<Vec<u8>> {
        let rtn = match self {
            MessageCmds::Ping => {
                tracing::info!("creating ping message");

                Vec::from(b"ping")
            },
            MessageCmds::Rand { len } => {
                tracing::info!("creating rand bytes {}", len);

                let mut bytes = vec![0u8; *len];

                rand::thread_rng()
                    .try_fill_bytes(&mut bytes)
                    .context("failed to generate random bytes")?;

                bytes
            },
            MessageCmds::AlphaRand { len } => {
                tracing::info!("creating rand alphanumeric bytes {}", len);

                let rand_string = Alphanumeric.sample_string(&mut rand::thread_rng(), *len);

                rand_string.into_bytes()
            },
            MessageCmds::Message { say } => {
                tracing::info!("creating message {}", say.len());

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

/// handles the bytes command
pub fn handle(mut stream: TcpStream, mut args: BytesArgs) -> Result<()> {
    let command = args.command.take().unwrap_or_default();

    if let Some(n_times) = args.repeat {
        for _ in 0..n_times {
            let bytes = command.get_message()?;

            write_to_socket(&mut stream, &bytes, args.buf_size)?;

            if !args.no_read {
                read_from_socket(&mut stream, bytes.len(), args.buf_size)?;
            }

            if let Some(ms) = &args.delay {
                let duration = std::time::Duration::from_millis(*ms);

                std::thread::sleep(duration);
            }
        }
    } else if args.repeat_inf {
        loop {
            let bytes = command.get_message()?;

            write_to_socket(&mut stream, &bytes, args.buf_size)?;

            if !args.no_read {
                read_from_socket(&mut stream, bytes.len(), args.buf_size)?;
            }

            if let Some(ms) = &args.delay {
                let duration = std::time::Duration::from_millis(*ms);

                std::thread::sleep(duration);
            }
        }
    } else {
        let bytes = command.get_message()?;

        write_to_socket(&mut stream, &bytes, args.buf_size)?;

        if !args.no_read {
            read_from_socket(&mut stream, bytes.len(), args.buf_size)?;
        }
    }

    Ok(())
}

/// writes the given bytes slice to the tcp stream
///
/// will write until all bytes of the given slice have been written to the
/// stream
fn write_to_socket(stream: &mut TcpStream, bytes: &[u8], chunk_size: usize) -> Result<()> {
    let mut total_wrote = 0usize;
    let mut start_index = 0usize;
    let mut end_index = if bytes.len() < chunk_size {
        bytes.len()
    } else {
        chunk_size
    };

    loop {
        tracing::info!("writing byte range {}..{}", start_index, end_index);

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

/// reads bytes from the tcp stream
///
/// will read until the expected amount of bytes have been received
fn read_from_socket(stream: &mut TcpStream, expected_read: usize, chunk_size: usize) -> Result<()> {
    let mut buffer = vec![0u8; chunk_size];
    let mut total_read = 0usize;

    loop {
        let read = stream.read(&mut buffer)
            .context("failed to read data from tcp stream")?;

        if read == 0 {
            tracing::info!("read 0 bytes client disconnected");
            break;
        }

        total_read += read;

        let mut msg = format!("[{}]:", read);

        for b in &buffer[0..read] {
            write!(&mut msg, " {:02x}", b).unwrap();
        }

        let cow = String::from_utf8_lossy(&buffer[0..read]);

        write!(&mut msg, "\n\u{2514}\u{2500}\"{}\"", cow).unwrap();

        tracing::info!("{}", msg);

        if total_read >= expected_read {
            tracing::warn!("read expected amount of bytes");
            break;
        }
    }

    Ok(())
}
