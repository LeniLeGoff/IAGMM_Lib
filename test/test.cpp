#include <iostream>
#include <fstream>
#include <random>
#include <memory>
#include <eigen3/Eigen/Core>
#include <eigen3/Eigen/Dense>
#include <eigen3/Eigen/Eigenvalues>
#include <math.h>
#include <tbb/tbb.h>
#include <ctime>

#include <boost/random.hpp>
#include <boost/chrono.hpp>

#include <iagmm/gmm.hpp>
#include <iagmm/component.hpp>

#include <boost/archive/binary_oarchive.hpp>

#include <SFML/Graphics.hpp>

#define MAX_X 100
#define MAX_Y 100
#define PI 3.14159265359
#define NBR_CLUSTER 2

using namespace cmm;

double compute_f(double A,double x, double y){
    return -(y+A)*sin(sqrt(abs(x/2+(y+A))))-x*sin(sqrt(abs(x-(y+A))));
}


int main(int argc, char** argv){
    srand(std::time(NULL));
    boost::random::mt19937 gen;
    boost::chrono::system_clock::time_point timer;

    tbb::task_scheduler_init init;

    double A;
    sf::RenderWindow window(sf::VideoMode(MAX_X*4*3,MAX_Y*4*2),"dataset");

    if(argc == 2)
        A = std::stod(argv[1]);
    else
        A = rand()%100;
    std::cout << "A = " << A << std::endl;
    int real_space[MAX_X][MAX_Y];
    double estimated_space[MAX_X][MAX_Y];
    std::vector<int> label;
    //    std::vector<Cluster::Ptr> model;
    CollabMM gmm(2,2);
    gmm.set_dataset_size_max(400);
    gmm.set_distance_function(
        [](const Eigen::VectorXd& s1,const Eigen::VectorXd& s2) -> double {
        return (s1 - s2).squaredNorm();
    });
    gmm.set_loglikelihood_driver(false);
    gmm.use_confidence(false);
    gmm.use_uncertainty(true);
    gmm.use_novelty(true);
    Eigen::VectorXd choice_dist_map = Eigen::VectorXd::Zero(MAX_Y*MAX_X);

    double error;


    std::multimap<double,Eigen::Vector2i> choice_distribution;
    double cumul_est;


    std::vector<sf::RectangleShape> rects_explored(MAX_Y*MAX_X,
                                                   sf::RectangleShape(sf::Vector2f(4,4)));
    std::vector<sf::RectangleShape> rects_real(MAX_Y*MAX_X,
                                               sf::RectangleShape(sf::Vector2f(4,4)));
    std::vector<sf::RectangleShape> rects_estimated(MAX_Y*MAX_X,
                                                    sf::RectangleShape(sf::Vector2f(4,4)));
    std::vector<sf::RectangleShape> rects_exact_est(MAX_Y*MAX_X,
                                                    sf::RectangleShape(sf::Vector2f(4,4)));
    std::vector<sf::RectangleShape> rects_confidence(MAX_Y*MAX_X,
                                                    sf::RectangleShape(sf::Vector2f(4,4)));

    std::vector<sf::CircleShape> components_center;

    std::vector<sf::RectangleShape> error_curve;

    Eigen::Vector2i coord(0,0);
    std::vector<std::pair<Eigen::VectorXd,std::vector<double>>> all_sample(MAX_Y*MAX_X);


    for(int i = 0; i < MAX_X*MAX_Y; i++){
        coord[0] = i%MAX_X;
        if(coord[0] == MAX_X - 1)
            coord[1]++;
        if(coord[1] >= MAX_Y)
            coord[1] = 0;

//        all_sample.push_back(Eigen::Vector2d(coord[0]/(double)MAX_X,coord[1]/(double)MAX_Y));

        if(compute_f(A,coord[0] - MAX_X/2,coord[1] - MAX_Y/2) > 0){
            real_space[coord[0]][coord[1]] = 1;
            rects_real[i].setFillColor(sf::Color(255,0,0));
        }else{
            real_space[coord[0]][coord[1]] = 0;
            rects_real[i].setFillColor(sf::Color(0,0,255));
        }
        estimated_space[coord[0]][coord[1]] = 0.;

        rects_estimated[i].setPosition(coord[0]*4+MAX_X*4,coord[1]*4);
        rects_estimated[i].setFillColor(sf::Color::White);
        rects_exact_est[i].setPosition(coord[0]*4+MAX_X*4*2,coord[1]*4);
        rects_exact_est[i].setFillColor(sf::Color::White);

        rects_real[i].setPosition(coord[0]*4,coord[1]*4);
        rects_explored[i].setPosition(coord[0]*4+MAX_X*4*2,coord[1]*4);
        rects_explored[i].setFillColor(sf::Color::Transparent);

        rects_confidence[i].setPosition(coord[0]*4+MAX_X*4*2,coord[1]*4+MAX_X*4);
        rects_confidence[i].setFillColor(sf::Color::White);

    }

    int iteration = 0;

    error = 1.;
    Eigen::VectorXd next_s;
    coord[0] = rand()%MAX_X;
    coord[1] = rand()%MAX_Y;
    while(window.isOpen()){
        timer  = boost::chrono::system_clock::now();
        sf::Event event;
        while (window.pollEvent(event))
        {
            if (event.type == sf::Event::Closed)
                window.close();
        }
        window.clear(sf::Color::White);

        for(auto rect: rects_estimated)
            window.draw(rect);
        for(auto rect: rects_real)
            window.draw(rect);
        for(auto rect: rects_exact_est)
            window.draw(rect);
        for(auto rect: rects_explored)
            window.draw(rect);
        for(auto rect: rects_confidence)
            window.draw(rect);
        for(auto circle: components_center)
            window.draw(circle);
        for(auto err : error_curve)
            window.draw(err);




        label.push_back(real_space[coord[0]][coord[1]]);

        int ind = gmm.append(Eigen::Vector2d((double)coord[0]/(double)MAX_X,(double)coord[1]/(double)MAX_Y),
              real_space[coord[0]][coord[1]]);


//        gmm._estimate_training_dataset();
        gmm.update_model(ind,real_space[coord[0]][coord[1]]);
//        gmm.update_dataset_thres();
//        gmm.compute_normalisation();
        std::cout << "NORMALISATION : " << gmm.get_normalisation() << std::endl;
        error = 0;


        if(gmm.get_samples().size() > NBR_CLUSTER){
            cumul_est = 0;
            choice_distribution.clear();
            tbb::parallel_for(tbb::blocked_range<size_t>(0,MAX_X*MAX_Y),
                              [&](const tbb::blocked_range<size_t>& r){
                for(int i = r.begin(); i != r.end(); ++i){
                    if(i%MAX_X == MAX_X - 1)
                        coord[1]++;
                    if(coord[1] >= MAX_Y)
                        coord[1] = 0;
                    std::vector<double> est_vect(2);

                    est_vect = gmm.compute_estimation(
                                Eigen::Vector2d((double)(i%MAX_X)/(double)MAX_X,(double)(i/MAX_X)/(double)MAX_Y));
                    all_sample[i] =
                                std::make_pair(
                                    Eigen::Vector2d((double)(i%MAX_X)/(double)MAX_X,(double)(i/MAX_X)/(double)MAX_Y),est_vect);
                    //                    double conf = gmm.confidence(Eigen::Vector2d((double)(i%MAX_X)/(double)MAX_X,(double)(i/MAX_X)/(double)MAX_Y));
//                    if(conf < 1e-03)
//                        conf = 0;
//                    rects_confidence[i].setFillColor(sf::Color(255*conf,255*conf,255*conf));

                    double dist = choice_dist_map(i);
                    rects_confidence[i].setFillColor(
                                sf::Color(255*dist,
                                          255*dist,
                                          255*dist)
                                );
                    error += 1-est_vect[real_space[i%MAX_X][i/MAX_X]];

                    rects_estimated[i].setFillColor(
                                sf::Color(255*est_vect[1],0,255*est_vect[0])
                                );
                }
            });
            next_s = all_sample[gmm.next_sample(all_sample,choice_dist_map)].first;
            coord[0] = next_s(0)*MAX_X;
            coord[1] = next_s(1)*MAX_Y;
        }else{
            coord[0] = rand()%MAX_X;
            coord[1] = rand()%MAX_Y;
        }


        for(int i = 0; i < MAX_X*MAX_Y; i++)
            rects_explored[i].setFillColor(sf::Color::Transparent);

        int x,y;
        for(int i = 0; i < gmm.get_samples().size(); i++){
            x = gmm.get_samples()[i].second[0]*MAX_X;
            y = gmm.get_samples()[i].second[1]*MAX_Y;
            rects_explored[x + y*MAX_Y].setFillColor(
                        sf::Color(255*real_space[x][y],0,255*(1-real_space[x][y]))
                    );
        }

        error = error/(double)(MAX_X*MAX_Y);
        sf::RectangleShape error_point(sf::Vector2f(4,4));
        error_point.setFillColor(sf::Color(0,255,0));
        error_point.setPosition(sf::Vector2f(iteration,(1-error)*400+400));
        error_curve.push_back(error_point);

        //        Eigen::VectorXd eigenval;
        //        Eigen::MatrixXd eigenvect;
        components_center.clear();
        for(const auto& components : gmm.model()){
            for(const auto& c : components.second){
//                std::cout << c->print_parameters();
                components_center.push_back(sf::CircleShape(5.));

                components_center.back().setFillColor(sf::Color(components.first*255,100,(components.first-1)*255));
                components_center.back().setPosition(c->get_mu()(0)*MAX_X*4+MAX_X*4,c->get_mu()(1)*MAX_Y*4);

                //            c->compute_eigenvalues(eigenval,eigenvect);
                //            std::cout << "[" << eigenval << "] -- [" << eigenvect << "]" << std::endl;
            }
        }


        std::cout << "_________________________________________________________________" << std::endl;
        std::cout << "error : " << error << std::endl;
//        std::cout << "loglikelihood : " << gmm.loglikelihood() << std::endl;
//        std::cout << gmm.print_info() << std::endl;
        std::cout << "total number of samples in the model : " << gmm.number_of_samples() << std::endl;
        std::cout << iteration << "-------------------------------------------------------------------" << std::endl;
        std::cout << "Time spent " << boost::chrono::duration_cast<boost::chrono::milliseconds>(boost::chrono::system_clock::now() - timer) << std::endl;
        std::cout << "_________________________________________________________________" << std::endl;

        iteration++;

        window.display();



        //        std::cin.ignore();
    }

    std::ofstream of("archive_gmm");
    boost::archive::binary_oarchive oarch(of);
    oarch << gmm;

    return 0;
}
