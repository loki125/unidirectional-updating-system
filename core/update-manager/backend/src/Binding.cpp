#include <pybind11/pybind11.h>
#include <pybind11/stl.h>  

#include "CoreService.hpp"

namespace py = pybind11;

PYBIND11_MODULE(core, m) {
    m.doc() = "C++ core service module";

    py::class_<CoreService>(m, "CoreService")
        .def(py::init<>())

        .def("process_and_broadcast",
            &CoreService::process_and_broadcast,
            py::call_guard<py::gil_scoped_release>(),
            py::arg("type"),
            py::arg("pkg"),
            py::arg("version"),
            py::arg("arch")
        )

        .def("get_package_instances",
            &CoreService::get_package_instances,
            py::arg("pkg"),
            py::arg("type")
        )

        .def("get_package_info",
            &CoreService::get_package_info,
            py::arg("type"),
            py::arg("pkg"),
            py::arg("version"),
            py::arg("arch")
        );
}