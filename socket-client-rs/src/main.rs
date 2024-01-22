use std::net::TcpStream;
use std::io::{Read, Write};
use std::default::Default;

use rand::RngCore;
use rand::distributions::{Alphanumeric, DistString};
use anyhow::{Context, Result};
use clap::{Parser, Subcommand};

mod bytes;

enum HostKind {
    Ip(IpAddr),
    Dns(String),
}

impl HostKind {
    fn parse(arg: &str) -> HostKind {
        if let Some(ip) = IpAddr::from_str(arg) {
            HostKind::Ip(ip)
        } else {
            HostKind::Dns(arg.into())
        }
    }
}

#[derive(Debug, Parser)]
struct CliArgs {
    /// local ip address to attach to
    #[arg(long, default_value="::")]
    local_ip: IpAddr,

    #[arg(long, default_value="0")]
    local_port: u16,

    /// remote ip addres to connect to
    #[arg(long, default_value="::", value_parser(HostKind::parse))]
    remote_host: IpAddr,

    /// remote port to connect to
    #[arg(long, default_value="9000")]
    remote_port: u16,

    #[command(subcommand)]
    command: Option<ExecCmds>,
}

#[derive(Debug, Subcommand)]
enum ExecCmds {
    /// communicate with a server using raw bytes
    Bytes(bytes::BytesArgs)
}

impl Default for ExecCmds {
    fn default() -> Self {
        ExecCmds::Bytes(Default::default())
    }
}

fn main() -> Result<()> {
    let args = CliArgs::parse();

    let mut stream = TcpStream::connect(":::9000")
        .context("failed connecting to tcp server")?;

    match args.command.unwrap_or_default() {
        ExecCmds::Bytes(given) => bytes::handle(stream, given),
    }
}
