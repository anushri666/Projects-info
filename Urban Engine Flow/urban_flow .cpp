#include <bits/stdc++.h>
using namespace std;

#define RESET  "\033[0m"
#define RED    "\033[31m"
#define GREEN  "\033[32m"
#define YELLOW "\033[33m"
#define CYAN   "\033[36m"

struct Edge {
    int to, cap, flow;
};

struct Graph {
    int V;
    vector<Edge> edges;
    vector<vector<int>> adj;

    Graph(int V) : V(V), adj(V) {}

    void addEdge(int u, int v, int cap) {
        adj[u].push_back(edges.size());
        edges.push_back({v, cap, 0});
        adj[v].push_back(edges.size());
        edges.push_back({u, 0, 0});
    }
};

// ---- Ford-Fulkerson ----

bool visited[105];

int dfs_ff(Graph& g, int u, int sink, int pushed) {
    if (u == sink) return pushed;
    visited[u] = true;
    for (int id : g.adj[u]) {
        Edge& e = g.edges[id];
        if (!visited[e.to] && e.cap > e.flow) {
            int d = dfs_ff(g, e.to, sink, min(pushed, e.cap - e.flow));
            if (d > 0) {
                e.flow += d;
                g.edges[id ^ 1].flow -= d;
                return d;
            }
        }
    }
    return 0;
}

int fordFulkerson(Graph& g, int src, int sink) {
    int flow = 0;
    while (true) {
        memset(visited, false, sizeof(visited));
        int pushed = dfs_ff(g, src, sink, INT_MAX);
        if (!pushed) break;
        flow += pushed;
    }
    return flow;
}

// ---- Dinic's Algorithm ----

struct Dinic {
    Graph& g;
    vector<int> level, ptr;

    Dinic(Graph& g) : g(g), level(g.V), ptr(g.V) {}

    bool bfs(int src, int sink) {
        fill(level.begin(), level.end(), -1);
        level[src] = 0;
        queue<int> q;
        q.push(src);
        while (!q.empty()) {
            int u = q.front(); q.pop();
            for (int id : g.adj[u]) {
                Edge& e = g.edges[id];
                if (level[e.to] == -1 && e.cap > e.flow) {
                    level[e.to] = level[u] + 1;
                    q.push(e.to);
                }
            }
        }
        return level[sink] != -1;
    }

    int dfs(int u, int sink, int pushed) {
        if (u == sink || !pushed) return pushed;
        for (int& cid = ptr[u]; cid < (int)g.adj[u].size(); cid++) {
            int id = g.adj[u][cid];
            Edge& e = g.edges[id];
            if (level[e.to] != level[u] + 1) continue;
            int d = dfs(e.to, sink, min(pushed, e.cap - e.flow));
            if (d > 0) {
                e.flow += d;
                g.edges[id ^ 1].flow -= d;
                return d;
            }
        }
        return 0;
    }

    int maxflow(int src, int sink) {
        int flow = 0;
        while (bfs(src, sink)) {
            fill(ptr.begin(), ptr.end(), 0);
            while (int pushed = dfs(src, sink, INT_MAX))
                flow += pushed;
        }
        return flow;
    }

    void minCut(int src) {
        bfs(src, -1);
        cout << RED << "\n[MIN-CUT] Bottleneck edges causing gridlock:\n" << RESET;
        for (int u = 0; u < g.V; u++) {
            if (level[u] == -1) continue;
            for (int id : g.adj[u]) {
                Edge& e = g.edges[id];
                if (level[e.to] == -1 && e.cap > 0)
                    cout << RED << "  Edge " << u << " -> " << e.to
                         << "  [" << e.flow << "/" << e.cap << "]\n" << RESET;
            }
        }
    }
};

// ---- Dijkstra ----

vector<int> dijkstra(int src, int V, vector<vector<pair<int,int>>>& wadj) {
    vector<int> dist(V, INT_MAX);
    priority_queue<pair<int,int>, vector<pair<int,int>>, greater<>> pq;
    dist[src] = 0;
    pq.push({0, src});
    while (!pq.empty()) {
        auto [d, u] = pq.top(); pq.pop();
        if (d > dist[u]) continue;
        for (auto [v, w] : wadj[u])
            if (dist[u] + w < dist[v]) {
                dist[v] = dist[u] + w;
                pq.push({dist[v], v});
            }
    }
    return dist;
}

// ---- Catalan Numbers ----

long long catalan(int n) {
    vector<long long> C(n + 1, 0);
    C[0] = C[1] = 1;
    for (int i = 2; i <= n; i++)
        for (int j = 0; j < i; j++)
            C[i] += C[j] * C[i - 1 - j];
    return C[n];
}

// ---- Terminal Visualizer ----

void visualize(Graph& g, int src, int sink) {
    cout << YELLOW << "\n[LIVE] Network flow state:\n" << RESET;
    for (int u = 0; u < g.V; u++) {
        for (int id : g.adj[u]) {
            Edge& e = g.edges[id];
            if (e.cap == 0) continue;
            int pct = e.flow * 10 / e.cap;
            string bar = "";
            for (int i = 0; i < 10; i++) bar += (i < pct ? "█" : "░");
            string col = (e.flow == e.cap) ? RED : (pct >= 7 ? YELLOW : GREEN);
            cout << col << "  [" << u << "->" << e.to << "] "
                 << "[" << bar << "] " << e.flow << "/" << e.cap;
            if (e.flow == e.cap) cout << "  SATURATED";
            cout << RESET << "\n";
            this_thread::sleep_for(chrono::milliseconds(100));
        }
    }
}

// ---- Main ----

int main() {
    int V = 6, src = 0, sink = 5;

    Graph g(V);
    g.addEdge(0, 1, 10);
    g.addEdge(0, 2, 10);
    g.addEdge(1, 2,  2);
    g.addEdge(1, 3,  8);
    g.addEdge(1, 4,  5);
    g.addEdge(2, 4,  7);
    g.addEdge(3, 5, 10);
    g.addEdge(4, 3,  3);
    g.addEdge(4, 5, 10);

    // Catalan - count valid non-crossing paths in restricted grid zones
    cout << CYAN << "[CATALAN] Valid non-intersecting grid routes:\n" << RESET;
    for (int n = 1; n <= 8; n++)
        cout << "  C(" << n << ") = " << catalan(n) << "\n";
    cout << GREEN << "  8x8 parade zone allows " << catalan(8) << " valid paths\n" << RESET;

    // Dijkstra - shortest path through city
    vector<vector<pair<int,int>>> wadj(V);
    wadj[0] = {{1,4},{2,8}};
    wadj[1] = {{3,8},{2,11}};
    wadj[2] = {{4,7}};
    wadj[3] = {{4,2},{5,9}};
    wadj[4] = {{5,10}};

    auto dist = dijkstra(0, V, wadj);
    cout << CYAN << "\n[DIJKSTRA] Shortest path costs from node 0:\n" << RESET;
    for (int i = 0; i < V; i++)
        cout << "  Node " << i << " : " << dist[i] << "\n";

    // Ford-Fulkerson
    Graph g_ff(V);
    g_ff.addEdge(0,1,10); g_ff.addEdge(0,2,10);
    g_ff.addEdge(1,2,2);  g_ff.addEdge(1,3,8);
    g_ff.addEdge(1,4,5);  g_ff.addEdge(2,4,7);
    g_ff.addEdge(3,5,10); g_ff.addEdge(4,3,3);
    g_ff.addEdge(4,5,10);
    int ff = fordFulkerson(g_ff, src, sink);
    cout << GREEN << "\n[FORD-FULKERSON] Max Flow = " << ff << " units/hr\n" << RESET;

    // Dinic's
    Dinic dinic(g);
    int df = dinic.maxflow(src, sink);
    cout << GREEN << "[DINIC] Max Flow = " << df << " units/hr\n" << RESET;

    // Min-cut
    dinic.minCut(src);

    // Visualize
    visualize(g, src, sink);

    cout << CYAN << "\n[DONE] Max flow = " << df
         << " | Bottleneck at min-cut | C(8) = " << catalan(8) << " paths\n" << RESET;

    return 0;
}
