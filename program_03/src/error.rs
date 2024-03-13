type BoxDynError = Box<dyn std::error::Error>;

pub type Result<T> = std::result::Result<T, Error>;

#[derive(Debug)]
pub struct Error {
    context: String,
    source: Option<BoxDynError>,
}

impl Error {
    pub fn new<C>(cxt: C) -> Self
    where
        C: Into<String>
    {
        Error {
            context: cxt.into(),
            source: None,
        }
    }
}

impl std::fmt::Display for Error {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match &self.source {
            Some(err) => if f.alternate() {
                write!(f, "{} {:#}", self.context, err)
            } else {
                write!(f, "{}", self.context)
            }
            None => write!(f, "{}", self.context)
        }
    }
}

impl std::error::Error for Error {
    fn source(&self) -> Option<&(dyn std::error::Error + 'static)> {
        self.source.as_ref().map(|v| &**v as _)
    }
}

pub trait Context<T, E> {
    fn context<C>(self, cxt: C) -> std::result::Result<T, Error>
    where
        C: Into<String>;

    fn with_context(self, cb: impl FnOnce() -> String) -> std::result::Result<T, Error>;
}

impl<T, E> Context<T, E> for std::result::Result<T, E>
where
    E: Into<BoxDynError>
{
    fn context<C>(self, cxt: C) -> std::result::Result<T, Error>
    where
        C: Into<String>
    {
        match self {
            Ok(v) => Ok(v),
            Err(err) => Err(Error {
                context: cxt.into(),
                source: Some(err.into()),
            })
        }
    }

    fn with_context(self, cb: impl FnOnce() -> String) -> std::result::Result<T, Error> {
        match self {
            Ok(v) => Ok(v),
            Err(err) => Err(Error {
                context: cb(),
                source: Some(err.into()),
            })
        }
    }
}

impl<T> Context<T, ()> for std::option::Option<T> {
    fn context<C>(self, cxt: C) -> std::result::Result<T, Error>
    where
        C: Into<String>
    {
        match self {
            Some(v) => Ok(v),
            None => Err(Error {
                context: cxt.into(),
                source: None,
            })
        }
    }

    fn with_context(self, cb: impl FnOnce() -> String) -> std::result::Result<T, Error> {
        match self {
            Some(v) => Ok(v),
            None => Err(Error {
                context: cb(),
                source: None,
            })
        }
    }
}
