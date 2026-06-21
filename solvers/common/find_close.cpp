// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 David Meeker

#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <cmath>
#include <string>
#include <algorithm>

int main(){
    std::ifstream f("/c/temp/test_orig.fem");
    std::string line;
    
    struct Pt { double x, y; int idx; int group; };
    std::vector<Pt> pts;
    
    while(std::getline(f, line)){
        if(line.find("[NumPoints]") != std::string::npos){
            auto eq = line.find('=');
            int n = std::stoi(line.substr(eq+1));
            for(int i = 0; i < n; i++){
                std::getline(f, line);
                std::istringstream ls(line);
                double x, y; int bm, grp;
                ls >> x >> y >> bm >> grp;
                pts.push_back({x, y, i, grp});
            }
            break;
        }
    }
    
    // Now also generate arc chord intermediate points
    f.clear(); f.seekg(0);
    struct Arc { int n0, n1; double arcLen, maxSide; int marker; };
    std::vector<Arc> arcs;
    while(std::getline(f, line)){
        if(line.find("[NumArcSegments]") != std::string::npos){
            auto eq = line.find('=');
            int n = std::stoi(line.substr(eq+1));
            for(int i = 0; i < n; i++){
                std::getline(f, line);
                std::istringstream ls(line);
                int n0, n1, bm, hidden, grp;
                double arcLen, maxSide, mySide;
                ls >> n0 >> n1 >> arcLen >> maxSide >> bm >> hidden >> grp >> mySide;
                arcs.push_back({n0, n1, arcLen, maxSide, -(bm > 0 ? bm+1 : 0)});
            }
            break;
        }
    }
    
    // Generate chord intermediate points for each arc
    int nOrigPts = pts.size();
    for(int ai = 0; ai < (int)arcs.size(); ai++){
        auto& arc = arcs[ai];
        double ax = pts[arc.n0].x, ay = pts[arc.n0].y;
        double bx = pts[arc.n1].x, by = pts[arc.n1].y;
        double d = std::sqrt((bx-ax)*(bx-ax)+(by-ay)*(by-ay));
        if(d < 1e-30) continue;
        
        double tta = arc.arcLen * M_PI / 180.0;
        double R = d / (2.0 * std::sin(tta/2.0));
        double tx = (bx-ax)/d, ty = (by-ay)/d;
        double h = std::sqrt(std::max(0.0, R*R - d*d/4.0));
        double cx = ax + (d/2.0)*tx + h*(-ty);
        double cy = ay + (d/2.0)*ty + h*(tx);
        
        int k = (int)std::ceil(arc.arcLen / arc.maxSide);
        if(k <= 1) continue;
        
        double dtheta = arc.arcLen * M_PI / (180.0 * k);
        double cosD = std::cos(dtheta), sinD = std::sin(dtheta);
        double px = ax, py = ay;
        
        for(int j = 0; j < k-1; j++){
            double dx = px - cx, dy = py - cy;
            px = cx + dx*cosD - dy*sinD;
            py = cy + dx*sinD + dy*cosD;
            pts.push_back({px, py, (int)pts.size(), -1}); // -1 = arc intermediate
        }
    }
    
    std::cout << "Total points: " << nOrigPts << " input + " << (pts.size()-nOrigPts) << " arc intermediates = " << pts.size() << "\n\n";
    
    // Find close pairs (input node vs arc intermediate)
    std::vector<std::tuple<double, int, int>> closePairs;
    for(int i = 0; i < nOrigPts; i++){
        for(int j = nOrigPts; j < (int)pts.size(); j++){
            double dx = pts[i].x - pts[j].x, dy = pts[i].y - pts[j].y;
            double dist = std::sqrt(dx*dx + dy*dy);
            if(dist < 0.01){ // within 0.01 units
                closePairs.push_back({dist, i, j});
            }
        }
    }
    
    std::sort(closePairs.begin(), closePairs.end());
    
    std::cout << "Close input-node / arc-intermediate pairs (dist < 0.01):\n";
    for(auto& [dist, i, j] : closePairs){
        std::cout << "  dist=" << dist << "  input node " << i 
                  << " (" << pts[i].x << "," << pts[i].y << " grp=" << pts[i].group << ")"
                  << " <-> arc pt " << j 
                  << " (" << pts[j].x << "," << pts[j].y << ")\n";
    }
    
    return 0;
}
