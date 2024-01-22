use std::net::{IpAddr, SocketAddr, TcpListener};
use std::default::Default;

use anyhow::{Context, Result};
use clap::{Parser, Subcommand};

mod bytes;

#[derive(Debug, Parser)]
struct CliArgs {
    /// local ip address to attach to
    #[arg(short = 'H', long, default_value="::")]
    host: IpAddr,

    /// local port to listen on
    #[arg(short, long, default_value="9000")]
    port: u16,

    #[command(subcommand)]
    command: Option<ServeCmds>
}

impl CliArgs {
    fn get_socket_addr(&self) -> SocketAddr {
        SocketAddr::from((self.host, self.port))
    }
}

#[derive(Debug, Subcommand)]
enum ServeCmds {
    /// logs in comming bytes and can echo results back
    Bytes(bytes::BytesArgs)
}

impl Default for ServeCmds {
    fn default() -> Self {
        ServeCmds::Bytes(Default::default())
    }
}

fn main() -> Result<()> {
    let args = CliArgs::parse();

    let listener = {
        let socket_addr = args.get_socket_addr();

        println!("attaching to socket addr: {}", socket_addr);

        TcpListener::bind(socket_addr)
            .context("failed binding to tcp address")?
    };

    match args.command.unwrap_or_default() {
        ServeCmds::Bytes(given) => bytes::handle(listener, given)?,
    }

    Ok(())
}
