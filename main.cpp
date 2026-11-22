// main.cpp - the web server (API layer).
// It only connects web requests to the classes in shop.hpp; the real logic is there.
// Uses cpp-httplib: https://github.com/yhirose/cpp-httplib
#include "httplib.h"  // must come before shop.hpp
#include <iostream>
#include "shop.hpp"

// Small helpers so every route replies in the same way
void sendJson(httplib::Response& res, int status, const string& body) {
    res.status = status;
    res.set_content(body, "application/json; charset=utf-8");
}

void sendError(httplib::Response& res, int status, const string& message) {
    sendJson(res, status, "{\"error\":\"" + jsonEscape(message) + "\"}");
}

int main() {
    Shop shop;
    Owner owner;
    shop.seed();

    httplib::Server server;
    server.set_mount_point("/", "./public");  // sends the web page from the public folder

    // ---------- Customer routes ----------

    // List all items with their reviews
    server.Get("/api/items", [&](const httplib::Request&, httplib::Response& res) {
        sendJson(res, 200, shop.itemsJson());
    });

    // Add a review to item <id>
    server.Post(R"(/api/items/(\d+)/reviews)", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            int id = stoi(req.matches[1]);
            Review review(req.get_param_value("author"), stoi(req.get_param_value("stars")),
                          req.get_param_value("text"));  // throws if the review is invalid
            shop.addReview(id, review);
            cout << "New review: " << review << "  (total " << Review::total() << ")\n";
            sendJson(res, 200, "{\"ok\":true}");
        } catch (const exception& e) {
            sendError(res, 400, e.what());
        }
    });

    // Reserve 1 unit of item <id> for 24 hours
    server.Post(R"(/api/items/(\d+)/reserve)", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            shop.reserve(stoi(req.matches[1]), req.get_param_value("name"), req.get_param_value("phone"));  // throws OutOfStockException if none left
            sendJson(res, 200, "{\"ok\":true}");
        } catch (const exception& e) {
            sendError(res, 400, e.what());
        }
    });

    // ---------- Owner routes ----------

    // Check the owner's password
    server.Post("/api/owner/login", [&](const httplib::Request& req, httplib::Response& res) {
        if (owner.login(req.get_param_value("password")))
            sendJson(res, 200, "{\"ok\":true}");
        else
            sendError(res, 401, "Wrong password");
    });

    // Sales and restock data, only if the password header is correct
    server.Get("/api/owner/report", [&](const httplib::Request& req, httplib::Response& res) {
        if (!owner.login(req.get_header_value("X-Password"))) {
            sendError(res, 401, "Not allowed");
            return;
        }
        sendJson(res, 200, shop.reportJson());
    });

    // Owner marks a customer's issue as resolved (needs the password header)
    server.Post("/api/owner/resolve", [&](const httplib::Request& req, httplib::Response& res) {
        if (!owner.login(req.get_header_value("X-Password"))) {
            sendError(res, 401, "Not allowed");
            return;
        }
        try {
            shop.resolveIssue(stoi(req.get_param_value("item")), stoi(req.get_param_value("review")));
            sendJson(res, 200, "{\"ok\":true}");
        } catch (const exception& e) {
            sendError(res, 400, e.what());
        }
    });

    // Owner confirms the customer bought the reserved item (needs the password header)
    server.Post("/api/owner/bought", [&](const httplib::Request& req, httplib::Response& res) {
        if (!owner.login(req.get_header_value("X-Password"))) {
            sendError(res, 401, "Not allowed");
            return;
        }
        try {
            shop.completeReservation(stoi(req.get_param_value("item")), stoi(req.get_param_value("reservation")));
            sendJson(res, 200, "{\"ok\":true}");
        } catch (const exception& e) {
            sendError(res, 400, e.what());
        }
    });

    cout << "Open http://localhost:8080\n";
    server.listen("0.0.0.0", 8080);
}
