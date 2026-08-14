pub mod util;
pub mod elf;

use pyo3::exceptions::PyOSError;
use pyo3::prelude::*;
use pyo3::wrap_pyfunction;
use pyo3::exceptions::PyRuntimeError;
use pyo3::types::PyBytes;
use crate::pyclass;
use crate::pymethods;

use std::error;
use std::fs::OpenOptions;
use std::fs::read_to_string;
use std::io::BufWriter;
use std::io::Write;

use crate::elf::ElfFile;
use crate::elf::ElfSegment;

fn hex_to_bytes(hex: &str) -> Vec<u8> {
    hex.as_bytes()
        .chunks_exact(2)
        .map(|pair| {
            let hi = (pair[0] as char).to_digit(16).unwrap();
            let lo = (pair[1] as char).to_digit(16).unwrap();
            ((hi << 4) | lo) as u8
        })
        .collect()
}

fn is_valid_driver(name: &str) -> bool {
    matches!(
        name,
        "timer_driver" | "serial_virt_tx" | "ethernet_driver" | "net_virt_tx"
            | "net_virt_rx" | "micropython" | "micropython_net_copier" | "nfs"
            | "nfs_net_copier" | "serial_driver"
    )
}

fn read_lines(filename: &str) -> Vec<String> {
    read_to_string(filename).unwrap().lines().map(String::from).collect()
}

#[pyclass]
pub struct PyElfFile {
    inner: ElfFile,
}

// so I can send over the elf segments
#[pyclass]
pub struct PyElfSegment {
    #[pyo3(get)]
    pub name: Option<String>,

    pub data: Vec<u8>,

    #[pyo3(get)]
    pub phys_addr: u64,

    #[pyo3(get)]
    pub virt_addr: u64,

    #[pyo3(get)]
    pub loadable: bool,
}
// we give it the from trait, that can be used with ElfSegment as an argument
impl From<&ElfSegment> for PyElfSegment {
    fn from(seg: &ElfSegment) -> Self {
        Self {
            name: seg.name.clone(),
            data: seg.data.clone(),
            phys_addr: seg.phys_addr,
            virt_addr: seg.virt_addr,
            loadable: seg.loadable,
        }
    }
}
#[pymethods]
impl PyElfSegment {
    #[getter]
    fn data<'py>(&self, py: Python<'py>) -> &'py PyBytes {
        PyBytes::new(py, &self.data)
    }
}

#[pymethods]
impl PyElfFile {
    #[new]
    pub fn from_path(path: String) -> PyResult<Self> {
        let elf = ElfFile::from_path(std::path::Path::new(&path))
            .map_err(PyRuntimeError::new_err)?;

        Ok(Self { inner: elf })
    }

    pub fn find_symbol(&self, name: String) -> PyResult<(u64, u64)> {
        self.inner
            .find_symbol(&name)
            .map_err(PyRuntimeError::new_err)
    }

    pub fn loadable_segments(&self) -> Vec<PyElfSegment> {
        self.inner.segments.iter().filter(|s| s.loadable).map(PyElfSegment::from).collect()
    }

    pub fn update_segments(&mut self, driver_name: String) -> PyResult<()> {
        if !is_valid_driver(&driver_name) {
            return Err(PyRuntimeError::new_err(format!(
                "Invalid driver name: {}",
                driver_name
            )));
        }

        let symbol_file_name = format!("{}_symbols.txt", &driver_name);
        let lines = read_lines(&symbol_file_name);
        for line in lines {
            println!("The line we got was: {}", line);
            let mut parts = line.split_whitespace();
            let symbol = parts.next().unwrap(); // can cause us to panick if none
            let hex = parts.next().unwrap(); // second part of our .txt file

            let value: Vec<u8> = hex_to_bytes(hex); // since the read function naturally has everything as chars, but we obviously need to have 
            println!("The value is: {:?}", &value);
            println!("The symbol is: {}", symbol);
            self.inner.write_symbol(symbol, &value).map_err(PyRuntimeError::new_err)?;
        }
        // self.inner.write_symbol("on_reload", 1); // as we are reloading we mark as 1, hopefully changes default value

        Ok(())
    }

    pub fn get_entry_point(&self) -> u64 {
        return self.inner.entry;
    }
}

// we need an argument for the GIL
#[pymodule]
fn python_abi(_py: Python, m: &PyModule) -> PyResult<()> {
    m.add_class::<PyElfFile>()?;
    m.add_class::<PyElfSegment>()?;
    Ok(())
}