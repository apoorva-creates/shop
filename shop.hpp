// shop.hpp - the logic of the Shop Review app (all the OOP concepts live here)
//
// Layout of this file:
//   1. Exceptions        - our own error types
//   2. Helpers           - jsonEscape, Serializable interface
//   3. Review            - one customer review
//   4. Item and children - products sold in the shop
//   5. Owner             - password check for the owner section
//   6. Shop              - manages all items
#pragma once
#include <algorithm>  // sort, remove_if (newest-first order, deleting old reviews)
#include <cctype>     // tolower (for spotting issue words)
#include <ctime>      // time_t, time (review dates)
#include <iomanip>    // setprecision (formats the average rating)
#include <iostream>   // ostream / operator<< for printing a Review
#include <memory>     // unique_ptr, make_unique (automatic memory cleanup)
#include <mutex>      // mutex, lock_guard (safe use from several threads)
#include <sstream>    // ostringstream (builds JSON text piece by piece)
#include <stdexcept>  // runtime_error (base of our exceptions)
#include <string>
#include <vector>

using namespace std;  // lets us write string instead of std::string

// ============================================================
// 1. EXCEPTIONS (Inheritance + Exception Handling)
// ============================================================
class InvalidReviewException : public runtime_error {
public:
    explicit InvalidReviewException(const string& message) : runtime_error(message) {}
};

class OutOfStockException : public runtime_error {
public:
    explicit OutOfStockException(const string& message) : runtime_error(message) {}
};

// ============================================================
// 2. HELPERS
// ============================================================

// Makes text safe to place inside a JSON string (escapes quotes, backslashes, new lines)
inline string jsonEscape(const string& text) {
    string out;
    for (char c : text) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\t': out += "\\t";  break;
            case '\r': break;
            default:   out += c;
        }
    }
    return out;
}

// Interface (Abstraction): any class that inherits this MUST provide toJson()
class Serializable {
public:
    virtual ~Serializable() = default;
    virtual string toJson() const = 0;  // "= 0" makes it pure virtual
};

// ============================================================
// 3. REVIEW (Encapsulation, Static member, Operator overloading)
// ============================================================
class Review : public Serializable {
private:  // data is hidden; it can only be set through the constructor
    string author_, text_;
    int stars_;
    int id_ = 0;              // unique number, used when the owner resolves an issue
    time_t date_;             // when the review was written
    bool issue_ = false;      // does the text talk about a problem?
    bool resolved_ = false;   // has the owner fixed that problem?
    inline static int totalReviews_ = 0;  // one counter shared by ALL reviews

public:
    // The constructor validates the data before the object is created
    Review(string author, int stars, string text, time_t date = time(nullptr))
        : author_(move(author)), text_(move(text)), stars_(stars), date_(date) {
        if (stars_ < 1 || stars_ > 5)
            throw InvalidReviewException("Stars must be 1 to 5");
        if (author_.empty() || text_.empty())
            throw InvalidReviewException("Name and review text are required");
        id_ = ++totalReviews_;
        issue_ = looksLikeIssue(text_);
    }

    int stars() const { return stars_; }
    static int total() { return totalReviews_; }  // static: called as Review::total()

    static constexpr time_t MAX_AGE = 3L * 365 * 24 * 60 * 60;  // 3 years in seconds

    int id() const { return id_; }
    time_t date() const { return date_; }
    bool isIssue() const { return issue_; }
    bool isResolved() const { return resolved_; }
    bool isTooOld() const { return time(nullptr) - date_ > MAX_AGE; }  // older than 3 years?
    void resolve() { resolved_ = true; }  // only the owner's action calls this

    // Simple check: does the text mention a problem?
    static bool looksLikeIssue(string text) {
        for (char& c : text) c = (char)tolower(c);
        for (const char* word : {"issue", "problem", "broken", "damaged", "defect", "not working",
                                 "stopped working", "leak", "disconnect", "refund"})
            if (text.find(word) != string::npos) return true;
        return false;
    }

    string toJson() const override {
        char day[11];
        strftime(day, sizeof day, "%Y-%m-%d", localtime(&date_));
        return "{\"id\":" + to_string(id_) + ",\"author\":\"" + jsonEscape(author_) +
               "\",\"stars\":" + to_string(stars_) + ",\"text\":\"" + jsonEscape(text_) +
               "\",\"date\":\"" + day + "\",\"issue\":" + (issue_ ? "true" : "false") +
               ",\"resolved\":" + (resolved_ ? "true" : "false") + "}";
    }

    // Operator overloading: lets us write  cout << review
    friend ostream& operator<<(ostream& os, const Review& r) {
        return os << r.author_ << " (" << r.stars_ << "/5): " << r.text_;
    }
};

// ============================================================
// RESERVATION: one unit held for a customer for 24 hours
// ============================================================
class Reservation : public Serializable {
private:
    int id_;
    string name_, phone_;
    time_t madeAt_;
    inline static int nextId_ = 0;

public:
    static constexpr time_t HOLD_SECONDS = 24 * 60 * 60;  // 24 hours

    Reservation(string name, string phone, time_t madeAt = time(nullptr))
        : id_(++nextId_), name_(move(name)), phone_(move(phone)), madeAt_(madeAt) {
        if (name_.empty() || phone_.size() != 10 ||
            !none_of(phone_.begin(), phone_.end(), [](char c) { return c < '0' || c > '9'; }))
            throw runtime_error("Enter your name and a 10-digit phone number");
    }

    int id() const { return id_; }
    string phone() const { return phone_; }
    bool isExpired() const { return time(nullptr) - madeAt_ > HOLD_SECONDS; }
    int hoursLeft() const {  // rounded up
        time_t left = HOLD_SECONDS - (time(nullptr) - madeAt_);
        return int((left + 3599) / 3600);
    }
    string toJson() const override {
        return "{\"id\":" + to_string(id_) + ",\"name\":\"" + jsonEscape(name_) + "\",\"phone\":\"" + phone_ +
               "\",\"hoursLeft\":" + to_string(hoursLeft()) + "}";
    }
};

// ============================================================
// 4. ITEM (abstract parent) and its child classes
//    Abstraction, Inheritance, Polymorphism, Composition
// ============================================================
class Item : public Serializable {
protected:  // visible to child classes, hidden from everyone else
    int id_;
    string name_, emoji_;
    double price_;
    vector<Review> reviews_;  // Composition: an Item HAS reviews
    int stock_ = 10;          // units left in the shop
    int sold_ = 0;            // units sold so far
    vector<Reservation> reservations_;  // Composition: units held for customers

public:
    static constexpr int RESTOCK_LEVEL = 5;  // below this many units, the owner must restock

    Item(int id, string name, string emoji, double price)
        : id_(id), name_(move(name)), emoji_(move(emoji)), price_(price) {}
    virtual ~Item() = default;  // virtual: child objects are deleted correctly via a parent pointer

    // Every child class MUST define these (pure virtual = abstract class)
    virtual string category() const = 0;
    virtual string details() const = 0;

    int id() const { return id_; }

    // ---- Reviews ----
    // Adds a review and keeps the list newest first
    void addReview(const Review& review) {
        reviews_.push_back(review);
        sort(reviews_.begin(), reviews_.end(),
             [](const Review& a, const Review& b) { return a.date() > b.date(); });
    }

    // Deletes reviews older than 3 years; returns how many were removed
    int removeOldReviews() {
        size_t before = reviews_.size();
        reviews_.erase(remove_if(reviews_.begin(), reviews_.end(),
                                 [](const Review& r) { return r.isTooOld(); }), reviews_.end());
        return int(before - reviews_.size());
    }

    // Owner action: marks one customer issue as resolved
    bool resolveReview(int reviewId) {
        for (auto& r : reviews_)
            if (r.id() == reviewId) { r.resolve(); return true; }
        return false;
    }

    double averageRating() const {
        if (reviews_.empty()) return 0;
        double sum = 0;
        for (const auto& r : reviews_) sum += r.stars();
        return sum / reviews_.size();
    }

    // Operator overloading: compare two items by rating (item1 < item2)
    bool operator<(const Item& other) const { return averageRating() < other.averageRating(); }

    // ---- Stock and sales (used by the owner section) ----
    void setStock(int stock, int sold) { stock_ = stock; sold_ = sold; }

    // ---- Reservations: a unit is held for 24 hours ----
    void reserve(string name, string phone, time_t when = time(nullptr)) {
        if (stock_ < 1) throw OutOfStockException("Out of stock");
        reservations_.push_back(Reservation(move(name), move(phone), when));  // throws if name or phone is invalid
        --stock_;  // the held unit is not available to other customers
    }

    bool hasReservation(const string& phone) const {
        return any_of(reservations_.begin(), reservations_.end(),
                      [&](const Reservation& r) { return r.phone() == phone; });
    }

    void expireReservations() {  // holds older than 24 hours go back to stock
        for (auto it = reservations_.begin(); it != reservations_.end();) {
            if (it->isExpired()) { ++stock_; it = reservations_.erase(it); }
            else ++it;
        }
    }

    // The customer came to the store and bought it: now it counts as a sale
    bool completeReservation(int reservationId) {
        for (auto it = reservations_.begin(); it != reservations_.end(); ++it)
            if (it->id() == reservationId) { reservations_.erase(it); ++sold_; return true; }
        return false;
    }

    bool needsRestock() const { return stock_ < RESTOCK_LEVEL; }

    // ---- JSON output ----
    // For customers: full item with reviews
    string toJson() const override {
        ostringstream o;
        o << "{\"id\":" << id_ << ",\"name\":\"" << jsonEscape(name_) << "\",\"emoji\":\"" << emoji_
          << "\",\"category\":\"" << category() << "\",\"price\":" << price_
          << ",\"stock\":" << stock_ << ",\"details\":\"" << jsonEscape(details())
          << "\",\"average\":" << fixed << setprecision(1) << averageRating() << ",\"reviews\":[";
        for (size_t i = 0; i < reviews_.size(); ++i) o << (i ? "," : "") << reviews_[i].toJson();
        o << "]}";
        return o.str();
    }

    // For the owner: sales numbers only
    string reportJson() const {
        ostringstream o;
        o << "{\"id\":" << id_ << ",\"name\":\"" << jsonEscape(name_) << "\",\"emoji\":\"" << emoji_
          << "\",\"sold\":" << sold_ << ",\"stock\":" << stock_ << ",\"revenue\":" << sold_ * price_
          << ",\"restock\":" << (needsRestock() ? "true" : "false") << ",\"reservations\":[";
        for (size_t i = 0; i < reservations_.size(); ++i) o << (i ? "," : "") << reservations_[i].toJson();
        o << "],\"issues\":[";
        bool first = true;  // list only the issues the owner has not resolved yet
        for (const auto& r : reviews_)
            if (r.isIssue() && !r.isResolved()) { o << (first ? "" : ",") << r.toJson(); first = false; }
        o << "]}";
        return o.str();
    }
};

// ---- Child classes: Inheritance + Method overriding ----
// Each one adds its own extra detail and says which category it belongs to.

class GroceryItem : public Item {
    string bestBefore_;
public:
    GroceryItem(int id, string name, string emoji, double price, string bestBefore)
        : Item(id, move(name), move(emoji), price), bestBefore_(move(bestBefore)) {}
    string category() const override { return "Groceries"; }
    string details() const override { return "Best before: " + bestBefore_; }
};

class ElectronicsItem : public Item {
    int warrantyMonths_;
public:
    ElectronicsItem(int id, string name, string emoji, double price, int warrantyMonths)
        : Item(id, move(name), move(emoji), price), warrantyMonths_(warrantyMonths) {}
    string category() const override { return "Electronics"; }
    string details() const override { return "Warranty: " + to_string(warrantyMonths_) + " months"; }
};

class ClothingItem : public Item {
    string sizes_;
public:
    ClothingItem(int id, string name, string emoji, double price, string sizes)
        : Item(id, move(name), move(emoji), price), sizes_(move(sizes)) {}
    string category() const override { return "Clothing"; }
    string details() const override { return "Sizes: " + sizes_; }
};

class HomeItem : public Item {
    string material_;
public:
    HomeItem(int id, string name, string emoji, double price, string material)
        : Item(id, move(name), move(emoji), price), material_(move(material)) {}
    string category() const override { return "Home"; }
    string details() const override { return "Material: " + material_; }
};

class BookItem : public Item {
    string author_;
public:
    BookItem(int id, string name, string emoji, double price, string author)
        : Item(id, move(name), move(emoji), price), author_(move(author)) {}
    string category() const override { return "Books"; }
    string details() const override { return "Author: " + author_; }
};

// ============================================================
// 5. OWNER (Encapsulation: the password is private)
// ============================================================
class Owner {
private:
    string password_ = "owner123";  // demo only; a real app would store a hash
public:
    bool login(const string& attempt) const { return attempt == password_; }
};

// ============================================================
// 6. SHOP - holds all items and does the work for the server
// ============================================================
class Shop {
private:
    vector<unique_ptr<Item>> items_;  // parent pointers: one list holds every kind of item
    mutable mutex mtx_;               // the server can handle several requests at once

    // Returns the item with this id, or nullptr if there is none
    Item* find(int id) {
        for (auto& item : items_)
            if (item->id() == id) return item.get();
        return nullptr;
    }

public:
    void add(unique_ptr<Item> item) { items_.push_back(move(item)); }

    void addReview(int itemId, const Review& review) {
        lock_guard<mutex> lock(mtx_);
        Item* item = find(itemId);
        if (!item) throw InvalidReviewException("Item not found");
        item->addReview(review);
    }

    void reserve(int itemId, string name, string phone) {
        lock_guard<mutex> lock(mtx_);
        for (auto& i : items_) {  // only one reservation at a time per phone number
            i->expireReservations();
            if (i->hasReservation(phone))
                throw runtime_error("You already have an active reservation. Please collect it first.");
        }
        Item* item = find(itemId);
        if (!item) throw runtime_error("Item not found");
        item->reserve(move(name), move(phone));
    }

    // Owner action: the customer bought the reserved item in the store
    void completeReservation(int itemId, int reservationId) {
        lock_guard<mutex> lock(mtx_);
        Item* item = find(itemId);
        if (!item || !item->completeReservation(reservationId)) throw runtime_error("Reservation not found");
    }

    void resolveIssue(int itemId, int reviewId) {
        lock_guard<mutex> lock(mtx_);
        Item* item = find(itemId);
        if (!item || !item->resolveReview(reviewId)) throw runtime_error("Issue not found");
    }

    // JSON for customers (polymorphism: each item describes itself in its own way)
    string itemsJson() {  // not const: it also deletes reviews older than 3 years
        lock_guard<mutex> lock(mtx_);
        for (auto& item : items_) {
            item->removeOldReviews();
            item->expireReservations();
        }
        string out = "[";
        for (size_t i = 0; i < items_.size(); ++i) out += (i ? "," : "") + items_[i]->toJson();
        return out + "]";
    }

    // JSON for the owner: sales and stock of every item
    string reportJson() {  // not const: it also returns expired reservations to stock
        lock_guard<mutex> lock(mtx_);
        for (auto& item : items_) item->expireReservations();
        string out = "[";
        for (size_t i = 0; i < items_.size(); ++i) out += (i ? "," : "") + items_[i]->reportJson();
        return out + "]";
    }

    // Sample data so the app has something to show
    void seed() {
        add(make_unique<GroceryItem>(1, "Alphonso Mangoes (1 kg)", "🥭", 249, "5 days from purchase"));
        add(make_unique<GroceryItem>(2, "Fresh Milk (1 L)", "🥛", 56, "2 days from purchase"));
        add(make_unique<ElectronicsItem>(3, "Wireless Earbuds", "🎧", 1499, 12));
        add(make_unique<ElectronicsItem>(4, "20W Fast Charger", "🔌", 699, 6));
        add(make_unique<ClothingItem>(5, "Cotton T-Shirt", "👕", 399, "S, M, L, XL"));
        add(make_unique<ClothingItem>(6, "Running Shoes", "👟", 2199, "6 to 11"));
        add(make_unique<GroceryItem>(7, "Basmati Rice (5 kg)", "🍚", 549, "12 months from purchase"));
        add(make_unique<GroceryItem>(8, "Farm Eggs (12)", "🥚", 96, "7 days from purchase"));
        add(make_unique<ElectronicsItem>(9, "Bluetooth Speaker", "🔊", 1299, 12));
        add(make_unique<ElectronicsItem>(10, "Power Bank 10000 mAh", "🔋", 999, 6));
        add(make_unique<ClothingItem>(11, "Denim Jacket", "🧥", 1899, "M, L, XL"));
        add(make_unique<ClothingItem>(12, "Cotton Socks (3 pairs)", "🧦", 199, "Free size"));
        add(make_unique<HomeItem>(13, "Non-stick Pan", "🍳", 899, "Aluminium"));
        add(make_unique<HomeItem>(14, "Mixing Bowl Set", "🥣", 449, "Stainless steel"));
        add(make_unique<HomeItem>(15, "Laundry Basket", "🧺", 349, "Plastic"));
        add(make_unique<HomeItem>(16, "LED Table Lamp", "💡", 649, "Plastic and metal"));
        add(make_unique<BookItem>(17, "Atomic Habits", "📘", 499, "James Clear"));
        add(make_unique<BookItem>(18, "Wings of Fire", "📗", 250, "A. P. J. Abdul Kalam"));
        add(make_unique<BookItem>(19, "The Alchemist", "📙", 299, "Paulo Coelho"));
        add(make_unique<BookItem>(20, "Rich Dad Poor Dad", "📕", 350, "Robert Kiyosaki"));

        // setStock(stock left, units sold)
        find(1)->setStock(12, 30);
        find(2)->setStock(3, 45);
        find(3)->setStock(8, 22);
        find(4)->setStock(2, 15);
        find(5)->setStock(20, 18);
        find(6)->setStock(4, 9);
        find(7)->setStock(25, 40);  find(8)->setStock(4, 70);   find(9)->setStock(10, 28);  find(10)->setStock(3, 33);
        find(11)->setStock(9, 14);  find(12)->setStock(40, 55); find(13)->setStock(7, 21);  find(14)->setStock(15, 12);
        find(15)->setStock(2, 8);   find(16)->setStock(11, 19); find(17)->setStock(18, 45); find(18)->setStock(6, 38);
        find(19)->setStock(22, 27); find(20)->setStock(5, 31);
        find(3)->reserve("Karan", "9876543210");  // one unit already reserved, so the owner sees a reservation

        auto ago = [](int days) { return time(nullptr) - days * 86400L; };  // date "days" days ago
        addReview(1, Review("Asha", 5, "Sweet and juicy, perfectly ripe.", ago(10)));
        addReview(1, Review("Ravi", 4, "Good quality, a couple were a bit soft.", ago(1500)));  // over 3 years: will be deleted
        addReview(3, Review("Dev", 4, "Clear sound, battery lasts all day.", ago(40)));
        addReview(3, Review("Lata", 2, "Right bud disconnects sometimes.", ago(5)));
        addReview(5, Review("Nisha", 4, "Comfortable fabric, true to size.", ago(100)));
        addReview(2, Review("Meena", 4, "Always fresh, delivered early.", ago(3)));
        addReview(2, Review("Sana", 3, "Packet leaked once, but the shop replaced it.", ago(60)));
        addReview(7, Review("Kiran", 5, "Long grains, cooks fluffy every time.", ago(12)));
        addReview(8, Review("Priya", 4, "Fresh eggs, none were cracked.", ago(2)));
        addReview(9, Review("Arjun", 5, "Loud and clear, the bass is great for the price.", ago(8)));
        addReview(10, Review("Zoya", 3, "Charges fast, but it gets warm. Small issue.", ago(30)));
        addReview(11, Review("Om", 5, "Looks great and fits perfectly.", ago(15)));
        addReview(12, Review("Gita", 4, "Soft and comfortable, good value.", ago(45)));
        addReview(13, Review("Sam", 5, "Nothing sticks and it heats evenly.", ago(6)));
        addReview(14, Review("Rhea", 4, "Sturdy bowls in nice sizes.", ago(70)));
        addReview(15, Review("Dev", 2, "The handle broke after two weeks.", ago(9)));
        addReview(16, Review("Aman", 5, "Bright light, perfect for studying.", ago(4)));
        addReview(17, Review("Isha", 5, "Easy to read and very practical.", ago(18)));
        addReview(18, Review("Ravi", 5, "Inspiring story, finished it in two days.", ago(25)));
        addReview(19, Review("Neha", 4, "A short, beautiful read.", ago(50)));
        addReview(20, Review("Tara", 3, "Good ideas, but the print is small.", ago(35)));
        Review fixed("Omar", 1, "Charger stopped working after a week, big problem.", ago(20));
        fixed.resolve();  // the owner already fixed this one, so it shows a tick
        addReview(4, fixed);
    }
};
