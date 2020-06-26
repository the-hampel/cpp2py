#pragma once
#include <vector>
#include <string>
#include <numpy/arrayobject.h>

#include "../traits.hpp"
#include "../pyref.hpp"
#include "../macros.hpp"
#include "../numpy_proxy.hpp"
#include "../py_converter.hpp"

namespace cpp2py {

  // Convert vector to numpy_proxy, WARNING: Deep Copy
  template <typename V> numpy_proxy make_numpy_proxy_from_vector(V &&v) {
    static_assert(is_instantiation_of_v<std::vector, std::decay_t<V>>, "Logic error");
    using value_type = typename std::remove_reference_t<V>::value_type;

    if constexpr (has_npy_type<value_type>) {
      auto *vec_heap        = new std::vector<value_type>{std::forward<V>(v)};
      auto delete_pycapsule = [](PyObject *capsule) {
        auto *ptr = static_cast<std::vector<value_type> *>(PyCapsule_GetPointer(capsule, "guard"));
        delete ptr;
      };
      PyObject *capsule = PyCapsule_New(vec_heap, "guard", delete_pycapsule);

      return {1, // rank
              npy_type<value_type>,
              (void *)vec_heap->data(),
              std::is_const_v<value_type>,
              std::vector<long>{long(vec_heap->size())}, // extents
              std::vector<long>{sizeof(value_type)},     // strides
              capsule};
    } else {
      std::vector<pyref> vobj(v.size());
      std::transform(begin(v), end(v), begin(vobj), [](auto &&x) {
        if constexpr (std::is_reference_v<V>) {
          return convert_to_python(x);
        } else { // vector passed as rvalue
          return convert_to_python(std::move(x));
        }
      });
      return make_numpy_proxy_from_vector(std::move(vobj));
    }
  }

  // Make a new vector from numpy view
  template <typename T> std::vector<T> make_vector_from_numpy_proxy(numpy_proxy const &p) {
    EXPECTS(p.extents.size() == 1);

    long size = p.extents[0];

    std::vector<T> v(size);

    if (p.element_type == npy_type<pyref>) {
      long step = p.strides[0] / sizeof(pyref);
      auto **data = static_cast<PyObject **>(p.data);
      for(long i = 0; i < size; ++i)
        v[i] = py_converter<std::decay_t<T>>::py2c(data[i * step]);
    } else {
      EXPECTS(p.strides[0] % sizeof(T) == 0);
      long step = p.strides[0] / sizeof(T);
      T *data = static_cast<T *>(p.data);
      for(long i = 0; i < size; ++i)
        v[i] = data[i * step];
    }

    return v;
  }

  // --------------------------------------

  template <typename T> struct py_converter<std::vector<T>> {

    template <typename V> static PyObject *c2py(V &&v) {
      static_assert(is_instantiation_of_v<std::vector, std::decay_t<V>>, "Logic error");
      return make_numpy_proxy_from_vector(std::forward<V>(v)).to_python();
    }

    // --------------------------------------

    static bool is_convertible(PyObject *ob, bool raise_exception) {
      _import_array();

      // Special case: 1-d ndarray of builtin type
      if (PyArray_Check(ob)) {
        PyArrayObject *arr = (PyArrayObject *)(ob);
#ifdef PYTHON_NUMPY_VERSION_LT_17
        int rank = arr->nd;
#else
        int rank = PyArray_NDIM(arr);
#endif
        if (PyArray_TYPE(arr) == npy_type<T> and rank == 1) return true;
      }

      if (!PySequence_Check(ob)) {
        if (raise_exception) { PyErr_SetString(PyExc_TypeError, ("Cannot convert "s + to_string(ob) + " to std::vector as it is not a sequence"s).c_str()); }
        return false;
      }

      pyref seq = PySequence_Fast(ob, "expected a sequence");
      int len   = PySequence_Size(ob);
      for (int i = 0; i < len; i++) {
        if (!py_converter<std::decay_t<T>>::is_convertible(PySequence_Fast_GET_ITEM((PyObject *)seq, i), raise_exception)) { // borrowed ref
          if (PyErr_Occurred()) PyErr_Print();
          return false;
        }
      }
      return true;
    }

    // --------------------------------------

    static std::vector<T> py2c(PyObject *ob) {
      _import_array();

      // Special case: 1-d ndarray of builtin type
      if (PyArray_Check(ob)) {
        PyArrayObject *arr = (PyArrayObject *)(ob);
#ifdef PYTHON_NUMPY_VERSION_LT_17
        int rank = arr->nd;
#else
        int rank = PyArray_NDIM(arr);
#endif
        if (rank == 1) return make_vector_from_numpy_proxy<T>(make_numpy_proxy(ob));
      }

      ASSERT(PySequence_Check(ob));
      std::vector<T> res;
      pyref seq = PySequence_Fast(ob, "expected a sequence");
      int len   = PySequence_Size(ob);
      for (int i = 0; i < len; i++) res.push_back(py_converter<std::decay_t<T>>::py2c(PySequence_Fast_GET_ITEM((PyObject *)seq, i))); //borrowed ref
      return res;
    }
  };

} // namespace cpp2py
