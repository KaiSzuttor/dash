#ifndef DASH__VIEW__VIEW_TRAITS_H__INCLUDED
#define DASH__VIEW__VIEW_TRAITS_H__INCLUDED


namespace dash {

template <class ViewT>
struct view_traits {

  // alternative: specialize struct view_traits for DimDiff := 0
  constexpr static bool is_projection = (ViewT::dimdiff != 0);

};

template <class ViewT>
typename ViewT::origin_type & origin(ViewT & view) {
  return view.origin();
}

} // namespace dash

#endif // DASH__VIEW__VIEW_TRAITS_H__INCLUDED