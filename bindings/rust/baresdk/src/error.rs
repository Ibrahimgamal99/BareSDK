use thiserror::Error;

#[derive(Debug, Error)]
pub enum Error {
    #[error("invalid argument (BARESDK_ERR_INVAL)")]
    Inval,
    #[error("out of memory (BARESDK_ERR_NOMEM)")]
    NoMem,
    #[error("wrong lifecycle state (BARESDK_ERR_STATE)")]
    State,
    #[error("DNS resolution failed (BARESDK_ERR_DNS)")]
    Dns,
    #[error("transport error (BARESDK_ERR_TRANSPORT)")]
    Transport,
    #[error("authentication failed (BARESDK_ERR_AUTH)")]
    Auth,
    #[error("server returned 5xx (BARESDK_ERR_SERVER_5XX)")]
    Server5xx,
    #[error("WebSocket protocol rejected (BARESDK_ERR_WS_PROTOCOL_REJECTED)")]
    WsProtocolRejected,
    #[error("timeout (BARESDK_ERR_TIMEOUT)")]
    Timeout,
    #[error("already in progress (BARESDK_ERR_ALREADY)")]
    Already,
    #[error("unknown error code {0}")]
    Unknown(i32),
}

impl From<i32> for Error {
    fn from(rc: i32) -> Self {
        match rc {
            -1  => Error::Inval,
            -2  => Error::NoMem,
            -3  => Error::State,
            -4  => Error::Dns,
            -5  => Error::Transport,
            -6  => Error::Auth,
            -7  => Error::Server5xx,
            -8  => Error::WsProtocolRejected,
            -9  => Error::Timeout,
            -10 => Error::Already,
            n   => Error::Unknown(n),
        }
    }
}

pub(crate) fn check(rc: i32) -> Result<(), Error> {
    if rc == 0 { Ok(()) } else { Err(Error::from(rc)) }
}
