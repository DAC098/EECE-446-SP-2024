use std::net::{TcpListener, TcpStream};
use std::io::{Read, Write};
use std::default::Default;

use anyhow::Result;
use clap::Args;

#[derive(Debug, Args)]
pub struct BytesArgs {
    /// size of buffer to send and receive data with
    #[arg(long, default_value="2048")]
    buf_size: usize,

    /// echo results back to the connected client
    #[arg(long, default_value="true")]
    echo: bool
}

impl Default for BytesArgs {
    fn default() -> Self {
        BytesArgs {
            buf_size: 2048,
            echo: true,
        }
    }
}

pub fn handle(listener: TcpListener, args: BytesArgs) -> Result<()> {
    loop {
        println!("waiting for connection");

        let (socket, addr) = match listener.accept() {
            Ok(result) => result,
            Err(err) => {
                println!("failed to accept tcp client: {:#?}", err);
                continue;
            }
        };

        println!("connection from {}", addr);

        handle_stream(&args, socket);
    }
}

fn handle_stream(args: &BytesArgs, mut stream: TcpStream) {
    let mut buffer = vec![0u8; args.buf_size];

    loop {
        buffer.fill(0u8);

        println!("waiting for data");

        let read = match stream.read(&mut buffer) {
            Ok(read) => read,
            Err(err) => {
                println!("socket read error: {:#?}", err);
                continue;
            }
        };

        if read == 0 {
            println!("client disconnected");
            break;
        }

        print_bytes(read, &buffer);

        let wrote = match stream.write(&buffer[0..read]) {
            Ok(wrote) => wrote,
            Err(err) => {
                println!("socket write error: {:#?}", err);
                continue;
            }
        };

        println!("{} bytes written", wrote);
    }
}

fn print_bytes(read: usize, bytes: &[u8]) {
    print!("[{}]:", read);

    for b in &bytes[0..read] {
        print!(" {:02x}", b);
    }

    let cow = String::from_utf8_lossy(&bytes[0..read]);

    println!("\n\u{2514}\u{2500}\"{}\"", cow);
}
