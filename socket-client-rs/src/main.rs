use std::net::{TcpStream, SocketAddr, IpAddr};
use std::default::Default;
use std::str::FromStr;
use std::convert::Infallible;
use std::time::Duration;

use socket2::{Socket, Domain, Type};
use anyhow::{anyhow, Error, Context, Result};
use clap::{Parser, Subcommand};
use tracing_subscriber::{FmtSubscriber, EnvFilter};

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
    /// enables logging
    #[arg(long)]
    log: bool,

    /// local ip address to attach to
    #[arg(long)]
    local_ip: Option<IpAddr>,

    /// local port to attach to
    #[arg(long)]
    local_port: Option<u16>,

    /// remote ip addres to connect to
    #[arg(long, default_value="0.0.0.0", value_parser(HostKind::parse))]
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

    if args.log {
        std::env::set_var("RUST_LOG", "info");
    } else {
        std::env::set_var("RUST_LOG", "error");
    }

    FmtSubscriber::builder()
        .with_env_filter(EnvFilter::from_default_env())
        .init();

    let socket = connect_host(&args)?;
    let stream: TcpStream = socket.into();

    if let Ok(local) = stream.local_addr() {
        tracing::info!("local addr: {local}");
    }

    if let Ok(peer) = stream.peer_addr() {
        tracing::info!("peer addr: {peer}");
    }

    match args.command.unwrap_or_default() {
        ExecCmds::Bytes(given) => bytes::handle(stream, given),
    }
}

/// binds to the specified local ip and port
fn bind_addr(socket: &Socket, ip: IpAddr, port: u16) -> Result<()> {
    let local_addr = SocketAddr::from((ip, port));

    tracing::info!("attempting to bind to {}", local_addr);

    let sock_addr = local_addr.into();

    socket.bind(&sock_addr)
        .context("failed binding to local socket addr")
}

/// attempts to connect to remote ip and port
fn connect_addr(args: &CliArgs, addr: SocketAddr) -> Result<Socket> {
    let domain = if addr.is_ipv4() {
        Domain::IPV4
    } else {
        Domain::IPV6
    };

    let socket = Socket::new(domain, Type::STREAM, None)
        .context("failed to create socket")?;

    let sock_addr = addr.clone().into();

    socket.connect_timeout(&sock_addr, Duration::from_secs(3))
        .context("failed connecting to remote socket addr")?;

    Ok(socket)
}

/// attempts to connect the provided socket to the specified remote host
///
/// if the provided remote host is a dns name then it will perform the lookup 
/// and attempt to connect to each until one is successful. errors out if it
/// failed to connect to any ip address.
fn connect_host(args: &CliArgs) -> Result<Socket> {
    match &args.remote_host {
        HostKind::Ip(ip) => {
            let socket_addr = SocketAddr::from((ip.clone(), args.remote_port));

            connect_addr(args, socket_addr)
        }
        HostKind::Dns(host) => {
            tracing::info!("dns lookup on \"{}\"", host);

            let ips = dns_lookup::lookup_host(&host)
                .context("failed to lookup remote host")?;

            for ip in ips {
                let socket_addr = SocketAddr::from((ip, args.remote_port));

                match connect_addr(args, socket_addr) {
                    Ok(socket) => {
                        return Ok(socket);
                    }
                    Err(err) => {
                        println!("{:?}", err);
                    }
                }
            }

            Err(anyhow!("no more ip address to connect with"))
        }
    }
}
