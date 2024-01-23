use std::net::{SocketAddr, IpAddr};
use std::default::Default;
use std::str::FromStr;
use std::convert::Infallible;

use socket2::{Socket, Domain, Type};
use anyhow::{anyhow, Context, Result};
use clap::{Parser, Subcommand};

mod bytes;

/// the different kinds of remote hosts that can be attached to
#[derive(Debug, Clone)]
enum HostKind {
    Ip(IpAddr),
    Dns(String),
}

impl HostKind {
    /// parses command line arg to HostKind
    ///
    /// arg will be parsed as a valid IPv4/IPv6 address first then fallback to
    /// assume the argument is a dns entry to perform a lookup with.
    fn parse(arg: &str) -> Result<HostKind, Infallible> {
        if let Ok(ip) = IpAddr::from_str(arg) {
            Ok(HostKind::Ip(ip))
        } else {
            Ok(HostKind::Dns(arg.into()))
        }
    }
}

/// a simple command line utility for connecting to a remote host
///
/// currently only supports sending different kinds of raw bytes to the
/// specified remote host. defaults to bytes command
#[derive(Debug, Parser)]
struct CliArgs {
    /// local ip address to attach to
    #[arg(long, default_value="::")]
    local_ip: IpAddr,

    /// local port to attach to
    #[arg(long, default_value="0")]
    local_port: u16,

    /// remote ip addres to connect to
    #[arg(long, default_value="::", value_parser(HostKind::parse))]
    remote_host: HostKind,

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

    let socket = Socket::new(Domain::IPV6, Type::STREAM, None).context("failed to create socket")?;
    socket.set_only_v6(false).context("failed to set \"only_v6\" flag")?;

    bind_addr(&socket, &args)?;
    connect_host(&socket, &args)?;

    let stream = socket.into();

    match args.command.unwrap_or_default() {
        ExecCmds::Bytes(given) => bytes::handle(stream, given),
    }
}

/// binds to the specified local ip and port
fn bind_addr(socket: &Socket, args: &CliArgs) -> Result<()> {
    let local_addr = SocketAddr::from((args.local_ip.clone(), args.local_port));

    println!("attempting to bind to {}", local_addr);

    let sock_addr = local_addr.into();

    socket.bind(&sock_addr)
        .context("failed binding to local socket addr")
}

/// attempts to connect to remote ip and port
fn connect_addr(socket: &Socket, ip: IpAddr, port: u16) -> Result<()> {
    let remote_addr = SocketAddr::from((ip, port));

    println!("attempting to connect with {}", remote_addr);

    let sock_addr = remote_addr.into();

    socket.connect(&sock_addr)
        .context("failed connecting to remote socket addr")
}

/// attempts to connect the provided socket to the specified remote host
///
/// if the provided remote host is a dns name then it will perform the lookup 
/// and attempt to connect to each until one is successful. errors out if it
/// failed to connect to any ip address.
fn connect_host(socket: &Socket, args: &CliArgs) -> Result<()> {
    match &args.remote_host {
        HostKind::Ip(ip) => connect_addr(socket, ip.clone(), args.remote_port),
        HostKind::Dns(host) => {
            println!("dns lookup on \"{}\"", host);

            let ips = dns_lookup::lookup_host(&host)
                .context("failed to lookup remote host")?;

            for ip in ips {
                if let Err(err) = connect_addr(socket, ip, args.remote_port) {
                    println!("{}", err);
                } else {
                    return Ok(());
                }
            }

            Err(anyhow!("no more ip address to connect with"))
        }
    }
}
