#include <algorithm>
#include <string.h>
#include <vm.h>

class TCompareFunctor {
private:
    TBlock* m_compareCriteria;

public:
    TCompareFunctor(TBlock* criteria = NULL) : m_compareCriteria(criteria) { }

    bool operator() (const void* left, const void* right) const {
        const TObject* leftObject  = reinterpret_cast<const TObject*>(left);
        const TObject* rightObject = reinterpret_cast<const TObject*>(right);

        // If no criteria is provided, we use < as the default
        if (! m_compareCriteria) {
            // Here we may perform optimization for well known types
            if (isSmallInteger(leftObject) || isSmallInteger(rightObject)) {
                if (isSmallInteger(leftObject) && isSmallInteger(rightObject)) {
                    TInteger leftValue  = reinterpret_cast<TInteger>(leftObject);
                    TInteger rightValue = reinterpret_cast<TInteger>(rightObject);

                    return getIntegerValue(leftValue) < getIntegerValue(rightValue);
                } else
                    return false; // TODO Send message <
            } else if (leftObject->getClass() == globals.stringClass && rightObject->getClass() == globals.stringClass) {
                const uint8_t* leftString  = static_cast<const TByteObject*>(leftObject)->getBytes();
                const uint8_t* rightString = static_cast<const TByteObject*>(rightObject)->getBytes();

                return std::lexicographical_compare(
                    leftString,  leftString  + leftObject->getSize(),
                    rightString, rightString + rightObject->getSize()
                );
            }
            // TODO comparison for TSymbol
        }

        // TODO Send message m_compareCriteria value: leftObject value: rightObject
        return false;
    }
};

template<> TObjectArray* TObjectArray::sortBy(TObjectArray* args) {
    const std::size_t size = getSize();
    if (size < 2)
        return this;

    TBlock* criteria = args->getField<TBlock>(1);
    TCompareFunctor compare(criteria != globals.nilObject ? criteria : NULL);

    // Populating temporary array for sorting
    // TODO Implement non-copying logic
    std::vector<TObject*> elements;
    elements.reserve(size);
    std::copy(fields, fields + size, elements.begin());

    // Sorting elements using provided compare criteria
    std::sort(elements.begin(), elements.end(), compare);

    // Storing sorted collection back to the object
    std::copy(elements.begin(), elements.end(), fields);

    return this;
}
